#pragma once

#include "partition_recovery_types.h"
#include "failure_detector.h"
#include "../raft/raft_node.h"
#include "../distributed_transaction/distributed_transaction_coordinator.h"
#include "../mvcc/mvcc_manager.h"
#include <unordered_map>
#include <thread>
#include <condition_variable>

// 分区恢复管理器
class PartitionRecoveryManager {
public:
    PartitionRecoveryManager(
        const std::string& node_id,
        const FailureDetectorConfig& config,
        std::shared_ptr<PartitionRecoveryNetworkInterface> network,
        std::shared_ptr<RaftNode> raft_node,
        std::shared_ptr<DistributedTransactionCoordinator> txn_coordinator,
        std::shared_ptr<MVCCManager> mvcc_manager
    );
    
    ~PartitionRecoveryManager();
    
    // 生命周期管理
    bool start();
    void stop();
    bool is_running() const;
    
    // 分区恢复接口
    std::string begin_partition_recovery(const PartitionInfo& partition);
    bool complete_partition_recovery(const std::string& recovery_id);
    bool abort_partition_recovery(const std::string& recovery_id);
    
    // 恢复查询
    std::shared_ptr<PartitionRecoveryContext> get_recovery_context(const std::string& recovery_id);
    std::vector<std::string> get_active_recoveries() const;
    PartitionState get_partition_state() const;
    
    // 节点管理
    bool add_monitored_node(const std::string& node_id, const std::string& address, uint16_t port);
    bool remove_monitored_node(const std::string& node_id);
    std::vector<NodeInfo> get_cluster_nodes() const;
    
    // 数据一致性
    std::vector<ConsistencyConflict> detect_consistency_conflicts();
    bool resolve_consistency_conflict(const std::string& key, 
                                     const std::string& resolved_value,
                                     PartitionRecoveryStrategy strategy);
    bool synchronize_data_with_node(const std::string& node_id);
    
    // 脑裂预防
    bool is_in_majority_partition() const;
    bool should_accept_writes() const;
    void enter_read_only_mode();
    void exit_read_only_mode();
    
    // 故障处理回调
    void on_node_failure(const std::string& node_id, FailureType failure_type);
    void on_node_recovery(const std::string& node_id);
    void on_partition_detected(const PartitionInfo& partition);
    
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
    std::shared_ptr<RaftNode> raft_node_;
    std::shared_ptr<DistributedTransactionCoordinator> txn_coordinator_;
    std::shared_ptr<MVCCManager> mvcc_manager_;
    
    // 故障检测器
    std::unique_ptr<FailureDetector> failure_detector_;
    
    // 恢复上下文管理
    mutable std::mutex recovery_contexts_mutex_;
    std::unordered_map<std::string, std::shared_ptr<PartitionRecoveryContext>> active_recoveries_;
    std::unordered_map<std::string, std::shared_ptr<PartitionRecoveryContext>> completed_recoveries_;
    
    // 集群状态
    mutable std::mutex cluster_state_mutex_;
    PartitionState current_partition_state_;
    bool is_read_only_mode_;
    std::unordered_set<std::string> majority_partition_nodes_;
    
    // 工作线程
    std::atomic<bool> running_;
    std::thread recovery_thread_;
    std::thread consistency_checker_thread_;
    std::thread health_monitor_thread_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    PartitionRecoveryStats stats_;
    
    // 恢复ID生成
    std::atomic<uint64_t> next_recovery_id_;
    
    // 私有方法
    std::string generate_recovery_id();
    void recovery_main_loop();
    void consistency_checker_loop();
    void health_monitor_loop();
    
    // 分区恢复实现
    bool execute_partition_recovery(std::shared_ptr<PartitionRecoveryContext> context);
    bool recover_raft_consensus(const PartitionInfo& partition);
    bool recover_distributed_transactions(const PartitionInfo& partition);
    bool recover_data_consistency(const PartitionInfo& partition);
    
    // 数据同步
    bool sync_raft_log_with_node(const std::string& node_id);
    bool sync_mvcc_data_with_node(const std::string& node_id);
    bool sync_transaction_state_with_node(const std::string& node_id);
    
    // 冲突解决
    std::vector<ConsistencyConflict> detect_raft_log_conflicts(const std::vector<std::string>& nodes);
    std::vector<ConsistencyConflict> detect_mvcc_data_conflicts(const std::vector<std::string>& nodes);
    bool resolve_conflict_by_strategy(const ConsistencyConflict& conflict,
                                     PartitionRecoveryStrategy strategy,
                                     std::string& resolved_value);
    
    // 脑裂处理
    void update_majority_partition();
    bool determine_majority_partition(const std::vector<PartitionInfo>& partitions);
    void handle_split_brain_scenario(const std::vector<PartitionInfo>& partitions);
    
    // 健康监控
    void monitor_cluster_health();
    void check_raft_leader_health();
    void check_transaction_coordinator_health();
    void check_data_consistency_health();
    
    // 恢复策略
    PartitionRecoveryStrategy determine_recovery_strategy(const ConsistencyConflict& conflict);
    bool apply_majority_wins_strategy(const ConsistencyConflict& conflict, std::string& resolved_value);
    bool apply_last_writer_wins_strategy(const ConsistencyConflict& conflict, std::string& resolved_value);
    bool apply_merge_conflicts_strategy(const ConsistencyConflict& conflict, std::string& resolved_value);
    
    // 工具方法
    void update_statistics();
    void record_partition_detection(const PartitionInfo& partition);
    void record_partition_recovery(const std::string& recovery_id);
    void cleanup_completed_recoveries();
    
    // 状态管理
    void set_partition_state(PartitionState state);
    void notify_components_of_partition_state(PartitionState state);
    void pause_write_operations();
    void resume_write_operations();
};