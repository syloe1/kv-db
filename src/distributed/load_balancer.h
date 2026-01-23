#pragma once

#include "distributed_types.h"
#include <atomic>
#include <mutex>

namespace kvdb {
namespace distributed {

class LoadBalancer {
public:
    LoadBalancer(LoadBalanceStrategy strategy = LoadBalanceStrategy::ROUND_ROBIN);
    ~LoadBalancer();
    
    // 节点管理
    bool add_node(const NodeInfo& node);
    bool remove_node(const std::string& node_id);
    bool update_node_load(const std::string& node_id, double load_factor);
    std::vector<NodeInfo> get_all_nodes() const;
    
    // 负载均衡
    std::string select_node() const;
    std::string select_node_for_key(const std::string& key) const;
    std::vector<std::string> select_replica_nodes(int count) const;
    
    // 健康检查
    bool is_node_healthy(const std::string& node_id) const;
    void mark_node_unhealthy(const std::string& node_id);
    void mark_node_healthy(const std::string& node_id);
    
    // 策略管理
    void set_strategy(LoadBalanceStrategy strategy);
    LoadBalanceStrategy get_strategy() const;
    
    // 统计信息
    size_t get_node_count() const;
    size_t get_healthy_node_count() const;
    double get_average_load() const;
    
private:
    mutable std::mutex mutex_;
    LoadBalanceStrategy strategy_;
    std::unordered_map<std::string, NodeInfo> nodes_;
    mutable std::atomic<size_t> round_robin_index_{0};
    
    // 负载均衡算法
    std::string round_robin_select() const;
    std::string least_connections_select() const;
    std::string weighted_round_robin_select() const;
    std::string consistent_hash_select(const std::string& key) const;
    
    // 工具函数
    std::vector<std::string> get_healthy_nodes() const;
    uint64_t hash_key(const std::string& key) const;
};

} // namespace distributed
} // namespace kvdb