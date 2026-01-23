#pragma once

#include "distributed_types.h"
#include "shard_manager.h"
#include "load_balancer.h"
#include "failover_manager.h"
#include "../db/kv_db.h"
#include <memory>
#include <future>

namespace kvdb {
namespace distributed {

class DistributedKVDB {
public:
    DistributedKVDB();
    ~DistributedKVDB();
    
    // 初始化和配置
    bool initialize(const std::string& node_id, const std::string& host, int port);
    bool shutdown();
    
    // 节点管理
    bool add_node(const std::string& node_id, const std::string& host, int port);
    bool remove_node(const std::string& node_id);
    std::vector<NodeInfo> get_all_nodes() const;
    
    // 分片管理
    bool create_shard(const std::string& shard_id, const std::string& start_key, 
                     const std::string& end_key);
    bool delete_shard(const std::string& shard_id);
    bool add_replica(const std::string& shard_id, const std::string& node_id);
    bool remove_replica(const std::string& shard_id, const std::string& node_id);
    
    // 分布式操作
    std::future<DistributedResponse> get_async(const std::string& key);
    std::future<DistributedResponse> put_async(const std::string& key, const std::string& value);
    std::future<DistributedResponse> delete_async(const std::string& key);
    std::future<DistributedResponse> scan_async(const std::string& start_key, 
                                               const std::string& end_key, int limit = 100);
    
    // 同步操作
    DistributedResponse get(const std::string& key);
    DistributedResponse put(const std::string& key, const std::string& value);
    DistributedResponse delete_key(const std::string& key);
    DistributedResponse scan(const std::string& start_key, const std::string& end_key, int limit = 100);
    
    // 事务支持
    class DistributedTransaction {
    public:
        DistributedTransaction(DistributedKVDB* db);
        ~DistributedTransaction();
        
        bool put(const std::string& key, const std::string& value);
        bool delete_key(const std::string& key);
        bool commit();
        bool rollback();
        
    private:
        DistributedKVDB* db_;
        std::vector<DistributedRequest> operations_;
        bool committed_;
    };
    
    std::unique_ptr<DistributedTransaction> begin_transaction();
    
    // 配置管理
    void set_replication_factor(int factor);
    int get_replication_factor() const;
    void set_consistency_level(const std::string& level);
    std::string get_consistency_level() const;
    
    // 监控和统计
    struct ClusterStats {
        size_t total_nodes;
        size_t healthy_nodes;
        size_t total_shards;
        size_t failed_shards;
        double average_load;
        size_t total_operations;
        double average_latency_ms;
    };
    
    ClusterStats get_cluster_stats() const;
    bool is_cluster_healthy() const;
    
private:
    std::string node_id_;
    std::string host_;
    int port_;
    bool initialized_;
    
    // 核心组件
    std::unique_ptr<ShardManager> shard_manager_;
    std::unique_ptr<LoadBalancer> load_balancer_;
    std::unique_ptr<FailoverManager> failover_manager_;
    std::unique_ptr<KVDB> local_db_;  // 本地存储
    
    // 配置
    int replication_factor_;
    std::string consistency_level_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    std::atomic<size_t> total_operations_{0};
    std::atomic<double> total_latency_ms_{0.0};
    
    // 内部操作
    DistributedResponse execute_request(const DistributedRequest& request);
    std::vector<std::string> get_target_nodes(const std::string& key);
    std::vector<std::string> get_target_shards(const std::string& start_key, 
                                              const std::string& end_key);
    
    // 网络通信
    DistributedResponse send_request_to_node(const std::string& node_id, 
                                           const DistributedRequest& request);
    std::vector<DistributedResponse> send_request_to_nodes(const std::vector<std::string>& node_ids,
                                                          const DistributedRequest& request);
    
    // 一致性处理
    DistributedResponse handle_read_request(const DistributedRequest& request);
    DistributedResponse handle_write_request(const DistributedRequest& request);
    DistributedResponse merge_responses(const std::vector<DistributedResponse>& responses);
    
    // 工具函数
    void update_stats(double latency_ms);
    bool is_local_key(const std::string& key) const;
    DistributedResponse create_error_response(const std::string& error_message);
    DistributedResponse create_success_response(const std::string& value = "");
    DistributedResponse execute_local_request(const DistributedRequest& request);
};

} // namespace distributed
} // namespace kvdb