#include "failover_manager.h"
#include <iostream>
#include <chrono>

namespace kvdb {
namespace distributed {

FailoverManager::FailoverManager(ShardManager* shard_manager, LoadBalancer* load_balancer)
    : shard_manager_(shard_manager), load_balancer_(load_balancer) {}

FailoverManager::~FailoverManager() {
    stop_monitoring();
}

bool FailoverManager::start_monitoring() {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    
    if (monitoring_) {
        return false;  // 已经在监控
    }
    
    monitoring_ = true;
    stop_requested_ = false;
    monitor_thread_ = std::thread(&FailoverManager::monitor_loop, this);
    
    update_state(FailoverState::NORMAL, "Failover monitoring started");
    return true;
}

bool FailoverManager::stop_monitoring() {
    std::unique_lock<std::mutex> lock(monitor_mutex_);
    
    if (!monitoring_) {
        return false;  // 没有在监控
    }
    
    stop_requested_ = true;
    monitor_cv_.notify_all();
    lock.unlock();
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    monitoring_ = false;
    update_state(FailoverState::NORMAL, "Failover monitoring stopped");
    return true;
}

bool FailoverManager::is_monitoring() const {
    return monitoring_;
}

bool FailoverManager::detect_node_failure(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (!ping_node(node_id)) {
        node_failure_counts_[node_id]++;
        node_recovery_counts_[node_id] = 0;
        
        if (node_failure_counts_[node_id] >= failure_threshold_) {
            failed_nodes_.insert(node_id);
            load_balancer_->mark_node_unhealthy(node_id);
            
            std::cout << "Node " << node_id << " marked as failed" << std::endl;
            
            if (auto_failover_enabled_) {
                handle_node_failure(node_id);
            }
            
            return true;
        }
    } else {
        node_recovery_counts_[node_id]++;
        node_failure_counts_[node_id] = 0;
        
        if (failed_nodes_.count(node_id) && 
            node_recovery_counts_[node_id] >= recovery_threshold_) {
            failed_nodes_.erase(node_id);
            load_balancer_->mark_node_healthy(node_id);
            
            std::cout << "Node " << node_id << " recovered" << std::endl;
            handle_node_recovery(node_id);
        }
    }
    
    return false;
}

bool FailoverManager::detect_shard_failure(const std::string& shard_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (!check_shard_availability(shard_id)) {
        failed_shards_.insert(shard_id);
        
        std::cout << "Shard " << shard_id << " marked as failed" << std::endl;
        
        if (auto_failover_enabled_) {
            handle_shard_failure(shard_id);
        }
        
        return true;
    }
    
    return false;
}

std::vector<std::string> FailoverManager::get_failed_nodes() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return std::vector<std::string>(failed_nodes_.begin(), failed_nodes_.end());
}

std::vector<std::string> FailoverManager::get_failed_shards() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return std::vector<std::string>(failed_shards_.begin(), failed_shards_.end());
}

bool FailoverManager::recover_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (failed_nodes_.count(node_id)) {
        failed_nodes_.erase(node_id);
        load_balancer_->mark_node_healthy(node_id);
        node_failure_counts_[node_id] = 0;
        node_recovery_counts_[node_id] = 0;
        
        std::cout << "Manually recovered node " << node_id << std::endl;
        handle_node_recovery(node_id);
        return true;
    }
    
    return false;
}

bool FailoverManager::recover_shard(const std::string& shard_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (failed_shards_.count(shard_id)) {
        failed_shards_.erase(shard_id);
        
        std::cout << "Manually recovered shard " << shard_id << std::endl;
        handle_shard_recovery(shard_id);
        return true;
    }
    
    return false;
}

bool FailoverManager::promote_replica_to_primary(const std::string& shard_id, 
                                                const std::string& new_primary) {
    auto replicas = shard_manager_->get_replica_nodes(shard_id);
    
    if (std::find(replicas.begin(), replicas.end(), new_primary) == replicas.end()) {
        return false;  // 新主节点不是副本
    }
    
    if (shard_manager_->set_primary_node(shard_id, new_primary)) {
        std::cout << "Promoted " << new_primary << " to primary for shard " << shard_id << std::endl;
        return true;
    }
    
    return false;
}

void FailoverManager::set_auto_failover(bool enabled) {
    auto_failover_enabled_ = enabled;
}

bool FailoverManager::is_auto_failover_enabled() const {
    return auto_failover_enabled_;
}

void FailoverManager::set_failover_timeout(int seconds) {
    failover_timeout_ = seconds;
}

int FailoverManager::get_failover_timeout() const {
    return failover_timeout_;
}

FailoverState FailoverManager::get_failover_state() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_state_;
}

std::string FailoverManager::get_failover_status() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return status_message_;
}

void FailoverManager::set_health_check_interval(int seconds) {
    health_check_interval_ = seconds;
}

void FailoverManager::set_failure_threshold(int count) {
    failure_threshold_ = count;
}

void FailoverManager::set_recovery_threshold(int count) {
    recovery_threshold_ = count;
}

void FailoverManager::monitor_loop() {
    while (!stop_requested_) {
        try {
            check_node_health();
            check_shard_health();
            
            if (auto_failover_enabled_) {
                perform_auto_failover();
            }
        } catch (const std::exception& e) {
            std::cerr << "Error in failover monitor: " << e.what() << std::endl;
        }
        
        // 等待下一次检查
        std::unique_lock<std::mutex> lock(monitor_mutex_);
        monitor_cv_.wait_for(lock, std::chrono::seconds(health_check_interval_), 
                           [this] { return stop_requested_.load(); });
    }
}

void FailoverManager::check_node_health() {
    auto nodes = load_balancer_->get_all_nodes();
    
    for (const auto& node : nodes) {
        detect_node_failure(node.node_id);
    }
}

void FailoverManager::check_shard_health() {
    auto shards = shard_manager_->get_all_shards();
    
    for (const auto& shard : shards) {
        detect_shard_failure(shard.shard_id);
    }
}

void FailoverManager::perform_auto_failover() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (current_state_ != FailoverState::NORMAL) {
        return;  // 已经在处理故障转移
    }
    
    if (!failed_nodes_.empty() || !failed_shards_.empty()) {
        update_state(FailoverState::DETECTING, "Performing automatic failover");
        
        // 处理失败的分片
        for (const auto& shard_id : failed_shards_) {
            std::string new_primary = find_best_replica_for_promotion(shard_id);
            if (!new_primary.empty()) {
                promote_replica_to_primary(shard_id, new_primary);
            }
        }
        
        update_state(FailoverState::COMPLETED, "Automatic failover completed");
        
        // 一段时间后重置状态
        std::this_thread::sleep_for(std::chrono::seconds(5));
        update_state(FailoverState::NORMAL, "System back to normal");
    }
}

void FailoverManager::handle_node_failure(const std::string& node_id) {
    std::cout << "Handling failure of node " << node_id << std::endl;
    
    // 找到该节点上的所有分片
    auto shards = shard_manager_->get_all_shards();
    for (const auto& shard : shards) {
        if (shard.primary_node == node_id) {
            // 需要选择新的主节点
            std::string new_primary = find_best_replica_for_promotion(shard.shard_id);
            if (!new_primary.empty()) {
                promote_replica_to_primary(shard.shard_id, new_primary);
            }
        }
    }
}

void FailoverManager::handle_shard_failure(const std::string& shard_id) {
    std::cout << "Handling failure of shard " << shard_id << std::endl;
    
    std::string new_primary = find_best_replica_for_promotion(shard_id);
    if (!new_primary.empty()) {
        promote_replica_to_primary(shard_id, new_primary);
    }
}

void FailoverManager::handle_node_recovery(const std::string& node_id) {
    std::cout << "Handling recovery of node " << node_id << std::endl;
    
    // 节点恢复后可以重新参与负载均衡
    load_balancer_->mark_node_healthy(node_id);
}

void FailoverManager::handle_shard_recovery(const std::string& shard_id) {
    std::cout << "Handling recovery of shard " << shard_id << std::endl;
    
    // 分片恢复后可以重新提供服务
}

bool FailoverManager::ping_node(const std::string& node_id) {
    // 简化实现：模拟节点健康检查
    // 实际实现应该发送网络请求检查节点状态
    
    auto nodes = load_balancer_->get_all_nodes();
    for (const auto& node : nodes) {
        if (node.node_id == node_id) {
            // 检查心跳超时
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - node.last_heartbeat);
            return elapsed.count() < failover_timeout_;
        }
    }
    
    return false;
}

bool FailoverManager::check_shard_availability(const std::string& shard_id) {
    // 简化实现：检查分片的主节点是否可用
    std::string primary_node = shard_manager_->get_primary_node(shard_id);
    
    if (primary_node.empty()) {
        return false;
    }
    
    return !failed_nodes_.count(primary_node);
}

std::string FailoverManager::find_best_replica_for_promotion(const std::string& shard_id) {
    auto replicas = shard_manager_->get_replica_nodes(shard_id);
    
    // 选择第一个健康的副本节点
    for (const auto& replica : replicas) {
        if (!failed_nodes_.count(replica) && load_balancer_->is_node_healthy(replica)) {
            return replica;
        }
    }
    
    return "";
}

void FailoverManager::update_state(FailoverState new_state, const std::string& message) {
    current_state_ = new_state;
    status_message_ = message;
    
    std::cout << "Failover state: " << static_cast<int>(new_state) 
              << ", Message: " << message << std::endl;
}

} // namespace distributed
} // namespace kvdb