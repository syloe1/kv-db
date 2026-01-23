#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>

// 网络分区状态
enum class PartitionState {
    NORMAL,         // 正常状态
    SUSPECTED,      // 疑似分区
    PARTITIONED,    // 确认分区
    RECOVERING,     // 恢复中
    RECOVERED       // 已恢复
};

// 节点健康状态
enum class NodeHealthState {
    HEALTHY,        // 健康
    SUSPECTED,      // 疑似故障
    FAILED,         // 故障
    RECOVERING,     // 恢复中
    ISOLATED        // 被隔离
};

// 分区恢复策略
enum class PartitionRecoveryStrategy {
    MAJORITY_WINS,      // 多数派获胜
    LAST_WRITER_WINS,   // 最后写入者获胜
    MANUAL_RESOLUTION,  // 手动解决
    MERGE_CONFLICTS     // 合并冲突
};

// 故障类型
enum class FailureType {
    NETWORK_PARTITION,  // 网络分区
    NODE_CRASH,         // 节点崩溃
    SLOW_NODE,          // 慢节点
    MESSAGE_LOSS,       // 消息丢失
    BYZANTINE_FAULT,    // 拜占庭故障
    UNKNOWN             // 未知故障
};

// 网络分区检测消息类型
enum class PartitionDetectionMessageType {
    HEARTBEAT,          // 心跳消息
    HEARTBEAT_ACK,      // 心跳确认
    PARTITION_SUSPECT,  // 分区怀疑
    PARTITION_CONFIRM,  // 分区确认
    RECOVERY_START,     // 开始恢复
    RECOVERY_COMPLETE,  // 恢复完成
    HEALTH_CHECK,       // 健康检查
    HEALTH_REPORT       // 健康报告
};

// 节点信息
struct NodeInfo {
    std::string node_id;                    // 节点ID
    std::string address;                    // 网络地址
    uint16_t port;                          // 端口
    NodeHealthState health_state;           // 健康状态
    std::chrono::system_clock::time_point last_heartbeat; // 最后心跳时间
    std::chrono::system_clock::time_point last_response;  // 最后响应时间
    uint64_t heartbeat_sequence;            // 心跳序列号
    double response_time_ms;                // 平均响应时间
    size_t consecutive_failures;            // 连续失败次数
    
    NodeInfo() : port(0), health_state(NodeHealthState::HEALTHY),
                heartbeat_sequence(0), response_time_ms(0.0), consecutive_failures(0) {}
    
    NodeInfo(const std::string& id, const std::string& addr, uint16_t p)
        : node_id(id), address(addr), port(p), health_state(NodeHealthState::HEALTHY),
          last_heartbeat(std::chrono::system_clock::now()),
          last_response(std::chrono::system_clock::now()),
          heartbeat_sequence(0), response_time_ms(0.0), consecutive_failures(0) {}
    
    bool is_healthy() const {
        return health_state == NodeHealthState::HEALTHY;
    }
    
    bool is_reachable(std::chrono::milliseconds timeout) const {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_response);
        return elapsed < timeout;
    }
};

// 分区信息
struct PartitionInfo {
    std::string partition_id;               // 分区ID
    PartitionState state;                   // 分区状态
    std::unordered_set<std::string> nodes;  // 分区内的节点
    std::chrono::system_clock::time_point detected_time; // 检测到分区的时间
    std::chrono::system_clock::time_point recovery_time; // 恢复时间
    PartitionRecoveryStrategy strategy;     // 恢复策略
    size_t majority_size;                   // 多数派大小
    
    PartitionInfo() : state(PartitionState::NORMAL),
                     strategy(PartitionRecoveryStrategy::MAJORITY_WINS),
                     majority_size(0) {}
    
    bool is_majority_partition(size_t total_nodes) const {
        return nodes.size() > total_nodes / 2;
    }
    
    bool contains_node(const std::string& node_id) const {
        return nodes.find(node_id) != nodes.end();
    }
};

// 分区检测消息
struct PartitionDetectionMessage {
    PartitionDetectionMessageType type;
    std::string sender_id;                  // 发送者ID
    std::string target_id;                  // 目标ID
    uint64_t sequence_number;               // 序列号
    uint64_t timestamp;                     // 时间戳
    std::unordered_set<std::string> reachable_nodes; // 可达节点列表
    std::string partition_id;               // 分区ID
    NodeHealthState health_state;           // 健康状态
    std::string additional_data;            // 附加数据
    
    PartitionDetectionMessage() : type(PartitionDetectionMessageType::HEARTBEAT),
                                 sequence_number(0), timestamp(0),
                                 health_state(NodeHealthState::HEALTHY) {}
};

// 故障检测器配置
struct FailureDetectorConfig {
    std::chrono::milliseconds heartbeat_interval{1000};    // 心跳间隔
    std::chrono::milliseconds failure_timeout{5000};       // 故障超时
    std::chrono::milliseconds recovery_timeout{30000};     // 恢复超时
    
    size_t max_consecutive_failures{3};                    // 最大连续失败次数
    size_t min_cluster_size{3};                            // 最小集群大小
    double failure_threshold{0.8};                         // 故障阈值
    
    bool enable_partition_detection{true};                 // 启用分区检测
    bool enable_auto_recovery{true};                       // 启用自动恢复
    bool enable_byzantine_detection{false};                // 启用拜占庭故障检测
    
    PartitionRecoveryStrategy default_recovery_strategy{PartitionRecoveryStrategy::MAJORITY_WINS};
    
    FailureDetectorConfig() = default;
    
    bool is_valid() const {
        return heartbeat_interval.count() > 0 &&
               failure_timeout > heartbeat_interval &&
               recovery_timeout > failure_timeout &&
               max_consecutive_failures > 0 &&
               min_cluster_size >= 1 &&
               failure_threshold > 0.0 && failure_threshold <= 1.0;
    }
};

// 分区恢复统计信息
struct PartitionRecoveryStats {
    size_t total_partitions_detected;      // 检测到的分区总数
    size_t partitions_recovered;           // 已恢复的分区数
    size_t node_failures_detected;         // 检测到的节点故障数
    size_t nodes_recovered;                // 已恢复的节点数
    
    double average_detection_time_ms;      // 平均检测时间
    double average_recovery_time_ms;       // 平均恢复时间
    double partition_recovery_rate;        // 分区恢复率
    
    size_t false_positives;                // 误报数
    size_t false_negatives;                // 漏报数
    double detection_accuracy;             // 检测准确率
    
    std::chrono::system_clock::time_point last_partition_time; // 最后一次分区时间
    std::chrono::system_clock::time_point last_recovery_time;  // 最后一次恢复时间
    
    PartitionRecoveryStats() : total_partitions_detected(0), partitions_recovered(0),
                              node_failures_detected(0), nodes_recovered(0),
                              average_detection_time_ms(0.0), average_recovery_time_ms(0.0),
                              partition_recovery_rate(0.0), false_positives(0),
                              false_negatives(0), detection_accuracy(1.0) {}
};

// 数据一致性冲突
struct ConsistencyConflict {
    std::string key;                        // 冲突的键
    std::vector<std::string> conflicting_values; // 冲突的值
    std::vector<std::string> nodes;         // 涉及的节点
    std::vector<uint64_t> timestamps;       // 时间戳
    std::vector<uint64_t> versions;         // 版本号
    PartitionRecoveryStrategy resolution_strategy; // 解决策略
    std::string resolved_value;             // 解决后的值
    bool is_resolved;                       // 是否已解决
    
    ConsistencyConflict() : resolution_strategy(PartitionRecoveryStrategy::LAST_WRITER_WINS),
                           is_resolved(false) {}
};

// 分区恢复上下文
class PartitionRecoveryContext {
public:
    PartitionRecoveryContext(const std::string& recovery_id);
    ~PartitionRecoveryContext();
    
    // 基本属性
    const std::string& get_recovery_id() const { return recovery_id_; }
    PartitionState get_state() const;
    void set_state(PartitionState state);
    
    // 分区管理
    void add_partition(const PartitionInfo& partition);
    void update_partition_state(const std::string& partition_id, PartitionState state);
    std::vector<PartitionInfo> get_partitions() const;
    PartitionInfo* get_partition(const std::string& partition_id);
    
    // 节点管理
    void add_node(const NodeInfo& node);
    void update_node_health(const std::string& node_id, NodeHealthState state);
    std::vector<NodeInfo> get_nodes() const;
    NodeInfo* get_node(const std::string& node_id);
    
    // 冲突管理
    void add_conflict(const ConsistencyConflict& conflict);
    void resolve_conflict(const std::string& key, const std::string& resolved_value);
    std::vector<ConsistencyConflict> get_unresolved_conflicts() const;
    bool has_unresolved_conflicts() const;
    
    // 时间管理
    std::chrono::system_clock::time_point get_start_time() const { return start_time_; }
    std::chrono::system_clock::time_point get_end_time() const { return end_time_; }
    void set_end_time(std::chrono::system_clock::time_point time) { end_time_ = time; }
    
    // 统计信息
    void increment_detection_count() { detection_count_++; }
    void increment_recovery_count() { recovery_count_++; }
    size_t get_detection_count() const { return detection_count_; }
    size_t get_recovery_count() const { return recovery_count_; }

private:
    std::string recovery_id_;
    PartitionState state_;
    
    std::vector<PartitionInfo> partitions_;
    std::vector<NodeInfo> nodes_;
    std::vector<ConsistencyConflict> conflicts_;
    
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
    
    std::atomic<size_t> detection_count_;
    std::atomic<size_t> recovery_count_;
    
    mutable std::mutex context_mutex_;
};