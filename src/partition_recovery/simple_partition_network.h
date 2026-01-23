#pragma once

#include "failure_detector.h"
#include <unordered_map>
#include <mutex>
#include <thread>
#include <random>

// 简单的分区恢复网络实现（用于测试）
class SimplePartitionRecoveryNetwork : public PartitionRecoveryNetworkInterface {
public:
    SimplePartitionRecoveryNetwork(const std::string& node_id);
    ~SimplePartitionRecoveryNetwork() override;
    
    // 消息发送
    bool send_message(const std::string& target_node,
                     const PartitionDetectionMessage& message) override;
    
    // 广播消息
    bool broadcast_message(const std::vector<std::string>& target_nodes,
                          const PartitionDetectionMessage& message) override;
    
    // 消息处理回调
    void set_message_handler(
        std::function<void(const PartitionDetectionMessage&)> handler) override;
    
    // 网络管理
    bool start(const std::string& address, uint16_t port) override;
    void stop() override;
    
    // 节点管理
    bool add_node(const std::string& node_id, const std::string& address, uint16_t port) override;
    bool remove_node(const std::string& node_id) override;
    bool is_node_reachable(const std::string& node_id) override;
    std::vector<std::string> get_reachable_nodes() override;
    
    // 网络测试
    double ping_node(const std::string& node_id) override;
    bool test_connectivity(const std::string& node_id) override;
    
    // 静态方法用于节点间通信
    static void register_network(const std::string& node_id, SimplePartitionRecoveryNetwork* network);
    static void unregister_network(const std::string& node_id);
    static SimplePartitionRecoveryNetwork* get_network(const std::string& node_id);
    
    // 网络分区模拟
    void simulate_partition(const std::vector<std::string>& partition1,
                           const std::vector<std::string>& partition2);
    void heal_partition();
    void set_network_failure_rate(double failure_rate);

private:
    std::string node_id_;
    std::function<void(const PartitionDetectionMessage&)> message_handler_;
    bool running_;
    
    // 对等节点信息
    std::mutex peers_mutex_;
    std::unordered_map<std::string, std::pair<std::string, uint16_t>> peers_;
    
    // 网络分区模拟
    mutable std::mutex partition_mutex_;
    bool is_partitioned_;
    std::unordered_set<std::string> reachable_nodes_;
    double network_failure_rate_;
    
    // 全局网络注册表
    static std::mutex global_networks_mutex_;
    static std::unordered_map<std::string, SimplePartitionRecoveryNetwork*> global_networks_;
    
    // 模拟网络延迟和丢包
    bool should_drop_message() const;
    void simulate_network_delay() const;
    bool can_reach_node(const std::string& node_id) const;
};