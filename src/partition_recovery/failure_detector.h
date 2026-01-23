#pragma once

#include "partition_recovery_types.h"
#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <functional>
#include <queue>

// 前向声明
class PartitionRecoveryNetworkInterface;

// 故障检测器
class FailureDetector {
public:
    FailureDetector(const std::string& node_id,
                   const FailureDetectorConfig& config,
                   std::shared_ptr<PartitionRecoveryNetworkInterface> network);
    
    ~FailureDetector();
    
    // 生命周期管理
    bool start();
    void stop();
    bool is_running() const;
    
    // 节点管理
    bool add_node(const std::string& node_id, const std::string& address, uint16_t port);
    bool remove_node(const std::string& node_id);
    std::vector<NodeInfo> get_monitored_nodes() const;
    
    // 故障检测
    bool is_node_healthy(const std::string& node_id) const;
    bool is_node_reachable(const std::string& node_id) const;
    std::vector<std::string> get_healthy_nodes() const;
    std::vector<std::string> get_failed_nodes() const;
    
    // 分区检测
    bool is_partitioned() const;
    std::vector<PartitionInfo> get_detected_partitions() const;
    PartitionInfo get_current_partition() const;
    
    // 消息处理
    void handle_message(const PartitionDetectionMessage& message);
    
    // 回调设置
    void set_failure_callback(std::function<void(const std::string&, FailureType)> callback);
    void set_recovery_callback(std::function<void(const std::string&)> callback);
    void set_partition_callback(std::function<void(const PartitionInfo&)> callback);
    
    // 统计信息
    PartitionRecoveryStats get_statistics() const;
    void print_statistics() const;
    
    // 配置管理
    void update_config(const FailureDetectorConfig& config);
    FailureDetectorConfig get_config() const;

private:
    std::string node_id_;
    FailureDetectorConfig config_;
    std::shared_ptr<PartitionRecoveryNetworkInterface> network_;
    
    // 节点监控
    mutable std::mutex nodes_mutex_;
    std::unordered_map<std::string, NodeInfo> monitored_nodes_;
    
    // 分区信息
    mutable std::mutex partitions_mutex_;
    std::vector<PartitionInfo> detected_partitions_;
    PartitionState current_partition_state_;
    
    // 消息队列
    mutable std::mutex message_queue_mutex_;
    std::queue<PartitionDetectionMessage> message_queue_;
    std::condition_variable message_queue_cv_;
    
    // 工作线程
    std::atomic<bool> running_;
    std::thread detector_thread_;
    std::thread heartbeat_thread_;
    std::thread analysis_thread_;
    
    // 回调函数
    std::function<void(const std::string&, FailureType)> failure_callback_;
    std::function<void(const std::string&)> recovery_callback_;
    std::function<void(const PartitionInfo&)> partition_callback_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    PartitionRecoveryStats stats_;
    
    // 序列号生成
    std::atomic<uint64_t> next_sequence_number_;
    
    // 私有方法
    void detector_main_loop();
    void heartbeat_loop();
    void analysis_loop();
    
    // 心跳处理
    void send_heartbeats();
    void send_heartbeat_to_node(const std::string& node_id);
    void handle_heartbeat(const PartitionDetectionMessage& message);
    void handle_heartbeat_ack(const PartitionDetectionMessage& message);
    
    // 故障检测
    void detect_node_failures();
    void detect_network_partitions();
    bool is_node_suspected_failed(const NodeInfo& node) const;
    void mark_node_failed(const std::string& node_id, FailureType failure_type);
    void mark_node_recovered(const std::string& node_id);
    
    // 分区检测
    void analyze_network_topology();
    void detect_partition_by_reachability();
    void create_partition_from_reachable_nodes(const std::unordered_set<std::string>& nodes);
    
    // 消息处理
    void handle_partition_suspect(const PartitionDetectionMessage& message);
    void handle_partition_confirm(const PartitionDetectionMessage& message);
    void handle_health_check(const PartitionDetectionMessage& message);
    void handle_health_report(const PartitionDetectionMessage& message);
    
    // 消息发送
    bool send_message(const std::string& target_id, const PartitionDetectionMessage& message);
    void broadcast_message(const PartitionDetectionMessage& message);
    
    // 工具方法
    uint64_t generate_sequence_number();
    uint64_t get_current_timestamp() const;
    void update_node_response_time(const std::string& node_id, double response_time_ms);
    void update_statistics();
    
    // 分区分析
    std::vector<std::unordered_set<std::string>> find_connected_components();
    bool are_nodes_connected(const std::string& node1, const std::string& node2) const;
    void merge_partitions_if_needed();
};

// 分区恢复网络接口
class PartitionRecoveryNetworkInterface {
public:
    virtual ~PartitionRecoveryNetworkInterface() = default;
    
    // 消息发送
    virtual bool send_message(const std::string& target_node,
                             const PartitionDetectionMessage& message) = 0;
    
    // 广播消息
    virtual bool broadcast_message(const std::vector<std::string>& target_nodes,
                                  const PartitionDetectionMessage& message) = 0;
    
    // 消息处理回调
    virtual void set_message_handler(
        std::function<void(const PartitionDetectionMessage&)> handler) = 0;
    
    // 网络管理
    virtual bool start(const std::string& address, uint16_t port) = 0;
    virtual void stop() = 0;
    
    // 节点管理
    virtual bool add_node(const std::string& node_id, const std::string& address, uint16_t port) = 0;
    virtual bool remove_node(const std::string& node_id) = 0;
    virtual bool is_node_reachable(const std::string& node_id) = 0;
    virtual std::vector<std::string> get_reachable_nodes() = 0;
    
    // 网络测试
    virtual double ping_node(const std::string& node_id) = 0; // 返回延迟(ms)，-1表示不可达
    virtual bool test_connectivity(const std::string& node_id) = 0;
};