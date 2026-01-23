#include "distributed_kvdb.h"
#include <iostream>
#include <chrono>
#include <algorithm>

namespace kvdb {
namespace distributed {

DistributedKVDB::DistributedKVDB() 
    : initialized_(false), replication_factor_(3), consistency_level_("quorum") {
    
    shard_manager_ = std::make_unique<ShardManager>();
    load_balancer_ = std::make_unique<LoadBalancer>();
    failover_manager_ = std::make_unique<FailoverManager>(shard_manager_.get(), load_balancer_.get());
    local_db_ = std::make_unique<KVDB>();
}

DistributedKVDB::~DistributedKVDB() {
    shutdown();
}

bool DistributedKVDB::initialize(const std::string& node_id, const std::string& host, int port) {
    if (initialized_) {
        return false;
    }
    
    node_id_ = node_id;
    host_ = host;
    port_ = port;
    
    // 初始化本地数据库
    if (!local_db_->open("distributed_" + node_id + ".db")) {
        return false;
    }
    
    // 添加自己到负载均衡器
    NodeInfo self_node(node_id, host, port);
    load_balancer_->add_node(self_node);
    
    // 启动故障转移监控
    failover_manager_->start_monitoring();
    
    initialized_ = true;
    std::cout << "Distributed KVDB initialized on " << host << ":" << port << std::endl;
    
    return true;
}

bool DistributedKVDB::shutdown() {
    if (!initialized_) {
        return false;
    }
    
    // 停止故障转移监控
    failover_manager_->stop_monitoring();
    
    // 关闭本地数据库
    local_db_->close();
    
    initialized_ = false;
    std::cout << "Distributed KVDB shutdown" << std::endl;
    
    return true;
}

bool DistributedKVDB::add_node(const std::string& node_id, const std::string& host, int port) {
    NodeInfo node(node_id, host, port);
    return load_balancer_->add_node(node);
}

bool DistributedKVDB::remove_node(const std::string& node_id) {
    return load_balancer_->remove_node(node_id);
}

std::vector<NodeInfo> DistributedKVDB::get_all_nodes() const {
    return load_balancer_->get_all_nodes();
}

bool DistributedKVDB::create_shard(const std::string& shard_id, const std::string& start_key, 
                                  const std::string& end_key) {
    if (!shard_manager_->create_shard(shard_id, start_key, end_key)) {
        return false;
    }
    
    // 自动分配副本节点
    auto replica_nodes = load_balancer_->select_replica_nodes(replication_factor_);
    for (const auto& node_id : replica_nodes) {
        shard_manager_->add_replica(shard_id, node_id);
    }
    
    // 设置主节点
    if (!replica_nodes.empty()) {
        shard_manager_->set_primary_node(shard_id, replica_nodes[0]);
    }
    
    return true;
}

bool DistributedKVDB::delete_shard(const std::string& shard_id) {
    return shard_manager_->delete_shard(shard_id);
}

bool DistributedKVDB::add_replica(const std::string& shard_id, const std::string& node_id) {
    return shard_manager_->add_replica(shard_id, node_id);
}

bool DistributedKVDB::remove_replica(const std::string& shard_id, const std::string& node_id) {
    return shard_manager_->remove_replica(shard_id, node_id);
}

std::future<DistributedResponse> DistributedKVDB::get_async(const std::string& key) {
    return std::async(std::launch::async, [this, key]() {
        return get(key);
    });
}

std::future<DistributedResponse> DistributedKVDB::put_async(const std::string& key, 
                                                           const std::string& value) {
    return std::async(std::launch::async, [this, key, value]() {
        return put(key, value);
    });
}

std::future<DistributedResponse> DistributedKVDB::delete_async(const std::string& key) {
    return std::async(std::launch::async, [this, key]() {
        return delete_key(key);
    });
}

std::future<DistributedResponse> DistributedKVDB::scan_async(const std::string& start_key, 
                                                            const std::string& end_key, int limit) {
    return std::async(std::launch::async, [this, start_key, end_key, limit]() {
        return scan(start_key, end_key, limit);
    });
}

DistributedResponse DistributedKVDB::get(const std::string& key) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    DistributedRequest request(DistributedOpType::GET, key);
    DistributedResponse response = handle_read_request(request);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    update_stats(duration.count() / 1000.0);
    
    return response;
}

DistributedResponse DistributedKVDB::put(const std::string& key, const std::string& value) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    DistributedRequest request(DistributedOpType::PUT, key, value);
    DistributedResponse response = handle_write_request(request);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    update_stats(duration.count() / 1000.0);
    
    return response;
}

DistributedResponse DistributedKVDB::delete_key(const std::string& key) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    DistributedRequest request(DistributedOpType::DELETE, key);
    DistributedResponse response = handle_write_request(request);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    update_stats(duration.count() / 1000.0);
    
    return response;
}

DistributedResponse DistributedKVDB::scan(const std::string& start_key, const std::string& end_key, int limit) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    DistributedRequest request(DistributedOpType::SCAN, "");
    request.start_key = start_key;
    request.end_key = end_key;
    request.limit = limit;
    
    // 获取涉及的分片
    auto target_shards = get_target_shards(start_key, end_key);
    std::vector<DistributedResponse> responses;
    
    for (const auto& shard_id : target_shards) {
        std::string primary_node = shard_manager_->get_primary_node(shard_id);
        if (!primary_node.empty()) {
            auto response = send_request_to_node(primary_node, request);
            if (response.success) {
                responses.push_back(response);
            }
        }
    }
    
    auto merged_response = merge_responses(responses);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    update_stats(duration.count() / 1000.0);
    
    return merged_response;
}

// DistributedTransaction 实现
DistributedKVDB::DistributedTransaction::DistributedTransaction(DistributedKVDB* db) 
    : db_(db), committed_(false) {}

DistributedKVDB::DistributedTransaction::~DistributedTransaction() {
    if (!committed_) {
        rollback();
    }
}

bool DistributedKVDB::DistributedTransaction::put(const std::string& key, const std::string& value) {
    operations_.emplace_back(DistributedOpType::PUT, key, value);
    return true;
}

bool DistributedKVDB::DistributedTransaction::delete_key(const std::string& key) {
    operations_.emplace_back(DistributedOpType::DELETE, key);
    return true;
}

bool DistributedKVDB::DistributedTransaction::commit() {
    if (committed_) {
        return false;
    }
    
    // 简化的两阶段提交
    // 第一阶段：准备
    std::unordered_map<std::string, std::vector<DistributedRequest>> node_operations;
    
    for (const auto& op : operations_) {
        auto target_nodes = db_->get_target_nodes(op.key);
        for (const auto& node_id : target_nodes) {
            node_operations[node_id].push_back(op);
        }
    }
    
    // 第二阶段：提交
    bool all_success = true;
    for (const auto& [node_id, ops] : node_operations) {
        for (const auto& op : ops) {
            auto response = db_->send_request_to_node(node_id, op);
            if (!response.success) {
                all_success = false;
                break;
            }
        }
        if (!all_success) break;
    }
    
    committed_ = all_success;
    return committed_;
}

bool DistributedKVDB::DistributedTransaction::rollback() {
    // 简化实现：清空操作列表
    operations_.clear();
    committed_ = true;  // 标记为已处理
    return true;
}

std::unique_ptr<DistributedKVDB::DistributedTransaction> DistributedKVDB::begin_transaction() {
    return std::make_unique<DistributedTransaction>(this);
}

void DistributedKVDB::set_replication_factor(int factor) {
    replication_factor_ = factor;
}

int DistributedKVDB::get_replication_factor() const {
    return replication_factor_;
}

void DistributedKVDB::set_consistency_level(const std::string& level) {
    consistency_level_ = level;
}

std::string DistributedKVDB::get_consistency_level() const {
    return consistency_level_;
}

DistributedKVDB::ClusterStats DistributedKVDB::get_cluster_stats() const {
    ClusterStats stats;
    
    stats.total_nodes = load_balancer_->get_node_count();
    stats.healthy_nodes = load_balancer_->get_healthy_node_count();
    stats.average_load = load_balancer_->get_average_load();
    
    auto shards = shard_manager_->get_all_shards();
    stats.total_shards = shards.size();
    
    auto failed_shards = failover_manager_->get_failed_shards();
    stats.failed_shards = failed_shards.size();
    
    stats.total_operations = total_operations_;
    stats.average_latency_ms = (total_operations_ > 0) ? 
                              (total_latency_ms_ / total_operations_) : 0.0;
    
    return stats;
}

bool DistributedKVDB::is_cluster_healthy() const {
    auto stats = get_cluster_stats();
    return (stats.healthy_nodes > stats.total_nodes / 2) && (stats.failed_shards == 0);
}

DistributedResponse DistributedKVDB::execute_request(const DistributedRequest& request) {
    if (request.op_type == DistributedOpType::GET || request.op_type == DistributedOpType::SCAN) {
        return handle_read_request(request);
    } else {
        return handle_write_request(request);
    }
}

std::vector<std::string> DistributedKVDB::get_target_nodes(const std::string& key) {
    std::string shard_id = shard_manager_->get_shard_for_key(key);
    if (shard_id.empty()) {
        return {};
    }
    
    return shard_manager_->get_replica_nodes(shard_id);
}

std::vector<std::string> DistributedKVDB::get_target_shards(const std::string& start_key, 
                                                           const std::string& end_key) {
    return shard_manager_->get_shards_for_range(start_key, end_key);
}

DistributedResponse DistributedKVDB::send_request_to_node(const std::string& node_id, 
                                                         const DistributedRequest& request) {
    // 如果是本地节点，直接处理
    if (node_id == node_id_) {
        return execute_local_request(request);
    }
    
    // 简化实现：模拟网络请求
    // 实际实现应该通过网络发送请求
    DistributedResponse response;
    response.success = true;
    
    switch (request.op_type) {
        case DistributedOpType::GET:
            response.value = "simulated_value_for_" + request.key;
            break;
        case DistributedOpType::PUT:
        case DistributedOpType::DELETE:
            response.success = true;
            break;
        case DistributedOpType::SCAN:
            response.results.emplace_back("key1", "value1");
            response.results.emplace_back("key2", "value2");
            break;
    }
    
    return response;
}

std::vector<DistributedResponse> DistributedKVDB::send_request_to_nodes(
    const std::vector<std::string>& node_ids, const DistributedRequest& request) {
    
    std::vector<DistributedResponse> responses;
    
    for (const auto& node_id : node_ids) {
        auto response = send_request_to_node(node_id, request);
        responses.push_back(response);
    }
    
    return responses;
}

DistributedResponse DistributedKVDB::handle_read_request(const DistributedRequest& request) {
    auto target_nodes = get_target_nodes(request.key);
    
    if (target_nodes.empty()) {
        return create_error_response("No target nodes found");
    }
    
    // 根据一致性级别选择读取策略
    if (consistency_level_ == "one") {
        // 从一个节点读取
        std::string node = load_balancer_->select_node_for_key(request.key);
        return send_request_to_node(node, request);
    } else if (consistency_level_ == "quorum") {
        // 从多数节点读取
        int quorum_size = (target_nodes.size() / 2) + 1;
        std::vector<std::string> selected_nodes(target_nodes.begin(), 
                                               target_nodes.begin() + std::min(quorum_size, 
                                                                              static_cast<int>(target_nodes.size())));
        
        auto responses = send_request_to_nodes(selected_nodes, request);
        return merge_responses(responses);
    } else {
        // 从所有节点读取
        auto responses = send_request_to_nodes(target_nodes, request);
        return merge_responses(responses);
    }
}

DistributedResponse DistributedKVDB::handle_write_request(const DistributedRequest& request) {
    auto target_nodes = get_target_nodes(request.key);
    
    if (target_nodes.empty()) {
        return create_error_response("No target nodes found");
    }
    
    // 根据一致性级别选择写入策略
    if (consistency_level_ == "one") {
        // 写入一个节点
        std::string primary_node = shard_manager_->get_primary_node(
            shard_manager_->get_shard_for_key(request.key));
        return send_request_to_node(primary_node, request);
    } else if (consistency_level_ == "quorum") {
        // 写入多数节点
        int quorum_size = (target_nodes.size() / 2) + 1;
        std::vector<std::string> selected_nodes(target_nodes.begin(), 
                                               target_nodes.begin() + std::min(quorum_size, 
                                                                              static_cast<int>(target_nodes.size())));
        
        auto responses = send_request_to_nodes(selected_nodes, request);
        
        // 检查是否有足够的成功响应
        int success_count = 0;
        for (const auto& response : responses) {
            if (response.success) {
                success_count++;
            }
        }
        
        if (success_count >= quorum_size) {
            return create_success_response();
        } else {
            return create_error_response("Quorum write failed");
        }
    } else {
        // 写入所有节点
        auto responses = send_request_to_nodes(target_nodes, request);
        
        // 检查是否所有写入都成功
        for (const auto& response : responses) {
            if (!response.success) {
                return create_error_response("All replicas write failed");
            }
        }
        
        return create_success_response();
    }
}

DistributedResponse DistributedKVDB::merge_responses(const std::vector<DistributedResponse>& responses) {
    if (responses.empty()) {
        return create_error_response("No responses to merge");
    }
    
    DistributedResponse merged;
    merged.success = false;
    
    // 简单的合并策略：选择第一个成功的响应
    for (const auto& response : responses) {
        if (response.success) {
            merged = response;
            break;
        }
    }
    
    // 对于扫描操作，合并所有结果
    if (!responses.empty() && responses[0].results.size() > 0) {
        merged.results.clear();
        for (const auto& response : responses) {
            if (response.success) {
                merged.results.insert(merged.results.end(), 
                                    response.results.begin(), response.results.end());
            }
        }
        merged.success = !merged.results.empty();
    }
    
    return merged;
}

void DistributedKVDB::update_stats(double latency_ms) {
    total_operations_++;
    total_latency_ms_ += latency_ms;
}

bool DistributedKVDB::is_local_key(const std::string& key) const {
    std::string shard_id = shard_manager_->get_shard_for_key(key);
    if (shard_id.empty()) {
        return false;
    }
    
    auto replicas = shard_manager_->get_replica_nodes(shard_id);
    return std::find(replicas.begin(), replicas.end(), node_id_) != replicas.end();
}

DistributedResponse DistributedKVDB::create_error_response(const std::string& error_message) {
    DistributedResponse response;
    response.success = false;
    response.error_message = error_message;
    return response;
}

DistributedResponse DistributedKVDB::create_success_response(const std::string& value) {
    DistributedResponse response;
    response.success = true;
    response.value = value;
    return response;
}

DistributedResponse DistributedKVDB::execute_local_request(const DistributedRequest& request) {
    DistributedResponse response;
    
    try {
        switch (request.op_type) {
            case DistributedOpType::GET: {
                std::string value;
                if (local_db_->get(request.key, value)) {
                    response.success = true;
                    response.value = value;
                } else {
                    response.success = false;
                    response.error_message = "Key not found";
                }
                break;
            }
            case DistributedOpType::PUT: {
                if (local_db_->put(request.key, request.value)) {
                    response.success = true;
                } else {
                    response.success = false;
                    response.error_message = "Put operation failed";
                }
                break;
            }
            case DistributedOpType::DELETE: {
                if (local_db_->delete_key(request.key)) {
                    response.success = true;
                } else {
                    response.success = false;
                    response.error_message = "Delete operation failed";
                }
                break;
            }
            case DistributedOpType::SCAN: {
                // 简化的扫描实现
                response.success = true;
                response.results.emplace_back("local_key1", "local_value1");
                response.results.emplace_back("local_key2", "local_value2");
                break;
            }
        }
    } catch (const std::exception& e) {
        response.success = false;
        response.error_message = e.what();
    }
    
    return response;
}

DistributedResponse DistributedKVDB::execute_local_request(const DistributedRequest& request) {
    DistributedResponse response;
    
    try {
        switch (request.op_type) {
            case DistributedOpType::GET: {
                std::string value;
                if (local_db_->get(request.key, value)) {
                    response.success = true;
                    response.value = value;
                } else {
                    response.success = false;
                    response.error_message = "Key not found";
                }
                break;
            }
            case DistributedOpType::PUT: {
                if (local_db_->put(request.key, request.value)) {
                    response.success = true;
                } else {
                    response.success = false;
                    response.error_message = "Put operation failed";
                }
                break;
            }
            case DistributedOpType::DELETE: {
                if (local_db_->delete_key(request.key)) {
                    response.success = true;
                } else {
                    response.success = false;
                    response.error_message = "Delete operation failed";
                }
                break;
            }
            case DistributedOpType::SCAN: {
                // 简化的扫描实现
                response.success = true;
                response.results.emplace_back("local_key1", "local_value1");
                response.results.emplace_back("local_key2", "local_value2");
                break;
            }
        }
    } catch (const std::exception& e) {
        response.success = false;
        response.error_message = e.what();
    }
    
    return response;
}

} // namespace distributed
} // namespace kvdb