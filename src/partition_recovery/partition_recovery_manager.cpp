
#include "partition_recovery_manager.h"
#include <iostream>
#include <algorithm>
#include <sstream>

PartitionRecoveryManager::PartitionRecoveryManager(
    const std::string& node_id,
    const FailureDetectorConfig& config,
    std::shared_ptr<PartitionRecoveryNetworkInterface> network,
    std::shared_ptr<RaftNode> raft_node,
    std::shared_ptr<DistributedTransactionCoordinator> txn_coordinator,
    std::shared_ptr<MVCCManager> mvcc_manager)
    : node_id_(node_id), config_(config), network_(network),
      raft_node_(raft_node), txn_coordinator_(txn_coordinator), mvcc_manager_(mvcc_manager),
      current_partition_state_(PartitionState::NORMAL), is_read_only_mode_(false),
      running_(false), next_recovery_id_(1) {
    
    // 创建故障检测器
    failure_detector_ = std::make_unique<FailureDetector>(node_id_, config_, network_);
    
    // 设置故障检测器回调
    failure_detector_->set_failure_callback([this](const std::string& node_id, FailureType type) {
        on_node_failure(node_id, type);
    });
    
    failure_detector_->set_recovery_callback([this](const std::string& node_id) {
        on_node_recovery(node_id);
    });
    
    failure_detector_->set_partition_callback([this](const PartitionInfo& partition) {
        on_partition_detected(partition);
    });
    
    std::cout << "PartitionRecoveryManager " << node_id_ << " initialized" << std::endl;
}

PartitionRecoveryManager::~PartitionRecoveryManager() {
    stop();
}

bool PartitionRecoveryManager::start() {
    if (running_.load()) {
        return false;
    }
    
    // 启动故障检测器
    if (!failure_detector_->start()) {
        std::cout << "Failed to start failure detector" << std::endl;
        return false;
    }
    
    running_.store(true);
    
    // 启动工作线程
    recovery_thread_ = std::thread(&PartitionRecoveryManager::recovery_main_loop, this);
    consistency_checker_thread_ = std::thread(&PartitionRecoveryManager::consistency_checker_loop, this);
    health_monitor_thread_ = std::thread(&PartitionRecoveryManager::health_monitor_loop, this);
    
    std::cout << "PartitionRecoveryManager " << node_id_ << " started" << std::endl;
    return true;
}
void PartitionRecoveryManager::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 等待线程结束
    if (recovery_thread_.joinable()) {
        recovery_thread_.join();
    }
    
    if (consistency_checker_thread_.joinable()) {
        consistency_checker_thread_.join();
    }
    
    if (health_monitor_thread_.joinable()) {
        health_monitor_thread_.join();
    }
    
    // 停止故障检测器
    failure_detector_->stop();
    
    std::cout << "PartitionRecoveryManager " << node_id_ << " stopped" << std::endl;
}

bool PartitionRecoveryManager::is_running() const {
    return running_.load();
}

std::string PartitionRecoveryManager::begin_partition_recovery(const PartitionInfo& partition) {
    std::string recovery_id = generate_recovery_id();
    
    auto context = std::make_shared<PartitionRecoveryContext>(recovery_id);
    context->add_partition(partition);
    context->set_state(PartitionState::RECOVERING);
    
    {
        std::lock_guard<std::mutex> lock(recovery_contexts_mutex_);
        active_recoveries_[recovery_id] = context;
    }
    
    std::cout << "Started partition recovery " << recovery_id 
              << " for partition " << partition.partition_id << std::endl;
    
    return recovery_id;
}

bool PartitionRecoveryManager::complete_partition_recovery(const std::string& recovery_id) {
    std::shared_ptr<PartitionRecoveryContext> context;
    
    {
        std::lock_guard<std::mutex> lock(recovery_contexts_mutex_);
        auto it = active_recoveries_.find(recovery_id);
        if (it == active_recoveries_.end()) {
            return false;
        }
        
        context = it->second;
        context->set_state(PartitionState::RECOVERED);
        context->set_end_time(std::chrono::system_clock::now());
        
        // 移动到已完成恢复
        completed_recoveries_[recovery_id] = context;
        active_recoveries_.erase(it);
    }
    
    record_partition_recovery(recovery_id);
    
    std::cout << "Completed partition recovery " << recovery_id << std::endl;
    return true;
}
bool PartitionRecoveryManager::abort_partition_recovery(const std::string& recovery_id) {
    std::shared_ptr<PartitionRecoveryContext> context;
    
    {
        std::lock_guard<std::mutex> lock(recovery_contexts_mutex_);
        auto it = active_recoveries_.find(recovery_id);
        if (it == active_recoveries_.end()) {
            return false;
        }
        
        context = it->second;
        context->set_state(PartitionState::NORMAL);
        context->set_end_time(std::chrono::system_clock::now());
        
        // 移动到已完成恢复
        completed_recoveries_[recovery_id] = context;
        active_recoveries_.erase(it);
    }
    
    std::cout << "Aborted partition recovery " << recovery_id << std::endl;
    return true;
}

std::shared_ptr<PartitionRecoveryContext> PartitionRecoveryManager::get_recovery_context(const std::string& recovery_id) {
    std::lock_guard<std::mutex> lock(recovery_contexts_mutex_);
    
    auto it = active_recoveries_.find(recovery_id);
    if (it != active_recoveries_.end()) {
        return it->second;
    }
    
    auto completed_it = completed_recoveries_.find(recovery_id);
    if (completed_it != completed_recoveries_.end()) {
        return completed_it->second;
    }
    
    return nullptr;
}

std::vector<std::string> PartitionRecoveryManager::get_active_recoveries() const {
    std::lock_guard<std::mutex> lock(recovery_contexts_mutex_);
    
    std::vector<std::string> recovery_ids;
    for (const auto& pair : active_recoveries_) {
        recovery_ids.push_back(pair.first);
    }
    
    return recovery_ids;
}

PartitionState PartitionRecoveryManager::get_partition_state() const {
    std::lock_guard<std::mutex> lock(cluster_state_mutex_);
    return current_partition_state_;
}

bool PartitionRecoveryManager::add_monitored_node(const std::string& node_id, const std::string& address, uint16_t port) {
    return failure_detector_->add_node(node_id, address, port);
}

bool PartitionRecoveryManager::remove_monitored_node(const std::string& node_id) {
    return failure_detector_->remove_node(node_id);
}
std::vector<NodeInfo> PartitionRecoveryManager::get_cluster_nodes() const {
    return failure_detector_->get_monitored_nodes();
}

std::vector<ConsistencyConflict> PartitionRecoveryManager::detect_consistency_conflicts() {
    std::vector<ConsistencyConflict> conflicts;
    
    // 获取所有健康节点
    auto healthy_nodes = failure_detector_->get_healthy_nodes();
    if (healthy_nodes.size() < 2) {
        return conflicts; // 需要至少2个节点才能检测冲突
    }
    
    // 检测Raft日志冲突
    auto raft_conflicts = detect_raft_log_conflicts(healthy_nodes);
    conflicts.insert(conflicts.end(), raft_conflicts.begin(), raft_conflicts.end());
    
    // 检测MVCC数据冲突
    auto mvcc_conflicts = detect_mvcc_data_conflicts(healthy_nodes);
    conflicts.insert(conflicts.end(), mvcc_conflicts.begin(), mvcc_conflicts.end());
    
    return conflicts;
}

bool PartitionRecoveryManager::resolve_consistency_conflict(const std::string& key, 
                                                           const std::string& resolved_value,
                                                           PartitionRecoveryStrategy strategy) {
    // 这里应该实现具体的冲突解决逻辑
    // 简化实现：直接应用解决值
    std::cout << "Resolving conflict for key " << key 
              << " with value " << resolved_value 
              << " using strategy " << static_cast<int>(strategy) << std::endl;
    
    return true;
}

bool PartitionRecoveryManager::synchronize_data_with_node(const std::string& node_id) {
    std::cout << "Synchronizing data with node " << node_id << std::endl;
    
    // 同步Raft日志
    if (!sync_raft_log_with_node(node_id)) {
        std::cout << "Failed to sync Raft log with " << node_id << std::endl;
        return false;
    }
    
    // 同步MVCC数据
    if (!sync_mvcc_data_with_node(node_id)) {
        std::cout << "Failed to sync MVCC data with " << node_id << std::endl;
        return false;
    }
    
    // 同步事务状态
    if (!sync_transaction_state_with_node(node_id)) {
        std::cout << "Failed to sync transaction state with " << node_id << std::endl;
        return false;
    }
    
    std::cout << "Successfully synchronized data with " << node_id << std::endl;
    return true;
}

bool PartitionRecoveryManager::is_in_majority_partition() const {
    std::lock_guard<std::mutex> lock(cluster_state_mutex_);
    
    auto healthy_nodes = failure_detector_->get_healthy_nodes();
    size_t total_nodes = healthy_nodes.size() + 1; // +1 for self
    size_t majority_size = total_nodes / 2 + 1;
    
    return healthy_nodes.size() >= majority_size - 1; // -1 because we don't count self in healthy_nodes
}
bool PartitionRecoveryManager::should_accept_writes() const {
    std::lock_guard<std::mutex> lock(cluster_state_mutex_);
    return !is_read_only_mode_ && is_in_majority_partition();
}

void PartitionRecoveryManager::enter_read_only_mode() {
    std::lock_guard<std::mutex> lock(cluster_state_mutex_);
    if (!is_read_only_mode_) {
        is_read_only_mode_ = true;
        pause_write_operations();
        std::cout << "Entered read-only mode" << std::endl;
    }
}

void PartitionRecoveryManager::exit_read_only_mode() {
    std::lock_guard<std::mutex> lock(cluster_state_mutex_);
    if (is_read_only_mode_) {
        is_read_only_mode_ = false;
        resume_write_operations();
        std::cout << "Exited read-only mode" << std::endl;
    }
}

void PartitionRecoveryManager::on_node_failure(const std::string& node_id, FailureType failure_type) {
    std::cout << "Handling node failure: " << node_id 
              << " (type: " << static_cast<int>(failure_type) << ")" << std::endl;
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.node_failures_detected++;
    }
    
    // 检查是否需要进入只读模式
    if (!is_in_majority_partition()) {
        enter_read_only_mode();
    }
    
    // 通知其他组件
    notify_components_of_partition_state(current_partition_state_);
}

void PartitionRecoveryManager::on_node_recovery(const std::string& node_id) {
    std::cout << "Handling node recovery: " << node_id << std::endl;
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.nodes_recovered++;
    }
    
    // 检查是否可以退出只读模式
    if (is_in_majority_partition()) {
        exit_read_only_mode();
    }
    
    // 同步数据
    synchronize_data_with_node(node_id);
}

void PartitionRecoveryManager::on_partition_detected(const PartitionInfo& partition) {
    std::cout << "Handling partition detection: " << partition.partition_id 
              << " with " << partition.nodes.size() << " nodes" << std::endl;
    
    // 记录分区检测
    record_partition_detection(partition);
    
    // 开始分区恢复
    std::string recovery_id = begin_partition_recovery(partition);
    
    // 更新分区状态
    set_partition_state(PartitionState::PARTITIONED);
    
    // 检查是否在多数派分区中
    if (!partition.is_majority_partition(get_cluster_nodes().size() + 1)) {
        enter_read_only_mode();
    }
}
PartitionRecoveryStats PartitionRecoveryManager::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void PartitionRecoveryManager::print_statistics() const {
    auto stats = get_statistics();
    
    std::cout << "\n=== Partition Recovery Statistics ===" << std::endl;
    std::cout << "Partitions Detected: " << stats.total_partitions_detected << std::endl;
    std::cout << "Partitions Recovered: " << stats.partitions_recovered << std::endl;
    std::cout << "Node Failures Detected: " << stats.node_failures_detected << std::endl;
    std::cout << "Nodes Recovered: " << stats.nodes_recovered << std::endl;
    std::cout << "Recovery Rate: " << (stats.partition_recovery_rate * 100) << "%" << std::endl;
    std::cout << "Detection Accuracy: " << (stats.detection_accuracy * 100) << "%" << std::endl;
    std::cout << "======================================" << std::endl;
}

void PartitionRecoveryManager::update_config(const FailureDetectorConfig& config) {
    config_ = config;
    failure_detector_->update_config(config);
}

FailureDetectorConfig PartitionRecoveryManager::get_config() const {
    return config_;
}

// 私有方法实现
std::string PartitionRecoveryManager::generate_recovery_id() {
    uint64_t id = next_recovery_id_.fetch_add(1);
    return "recovery_" + std::to_string(id);
}

void PartitionRecoveryManager::recovery_main_loop() {
    while (running_.load()) {
        // 处理活跃的恢复任务
        std::vector<std::shared_ptr<PartitionRecoveryContext>> active_contexts;
        
        {
            std::lock_guard<std::mutex> lock(recovery_contexts_mutex_);
            for (const auto& pair : active_recoveries_) {
                active_contexts.push_back(pair.second);
            }
        }
        
        for (auto& context : active_contexts) {
            if (context->get_state() == PartitionState::RECOVERING) {
                execute_partition_recovery(context);
            }
        }
        
        // 清理已完成的恢复
        cleanup_completed_recoveries();
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void PartitionRecoveryManager::consistency_checker_loop() {
    while (running_.load()) {
        // 检测一致性冲突
        auto conflicts = detect_consistency_conflicts();
        
        if (!conflicts.empty()) {
            std::cout << "Detected " << conflicts.size() << " consistency conflicts" << std::endl;
            
            // 尝试解决冲突
            for (const auto& conflict : conflicts) {
                std::string resolved_value;
                if (resolve_conflict_by_strategy(conflict, conflict.resolution_strategy, resolved_value)) {
                    resolve_consistency_conflict(conflict.key, resolved_value, conflict.resolution_strategy);
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
void PartitionRecoveryManager::health_monitor_loop() {
    while (running_.load()) {
        monitor_cluster_health();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

bool PartitionRecoveryManager::execute_partition_recovery(std::shared_ptr<PartitionRecoveryContext> context) {
    auto partitions = context->get_partitions();
    
    for (const auto& partition : partitions) {
        std::cout << "Executing recovery for partition " << partition.partition_id << std::endl;
        
        // 恢复Raft共识
        if (!recover_raft_consensus(partition)) {
            std::cout << "Failed to recover Raft consensus for partition " << partition.partition_id << std::endl;
            continue;
        }
        
        // 恢复分布式事务
        if (!recover_distributed_transactions(partition)) {
            std::cout << "Failed to recover distributed transactions for partition " << partition.partition_id << std::endl;
            continue;
        }
        
        // 恢复数据一致性
        if (!recover_data_consistency(partition)) {
            std::cout << "Failed to recover data consistency for partition " << partition.partition_id << std::endl;
            continue;
        }
        
        // 更新分区状态为已恢复
        context->update_partition_state(partition.partition_id, PartitionState::RECOVERED);
    }
    
    // 检查是否所有分区都已恢复
    bool all_recovered = true;
    for (const auto& partition : context->get_partitions()) {
        if (partition.state != PartitionState::RECOVERED) {
            all_recovered = false;
            break;
        }
    }
    
    if (all_recovered) {
        complete_partition_recovery(context->get_recovery_id());
        set_partition_state(PartitionState::RECOVERED);
        
        // 如果在多数派分区中，退出只读模式
        if (is_in_majority_partition()) {
            exit_read_only_mode();
        }
    }
    
    return all_recovered;
}

bool PartitionRecoveryManager::recover_raft_consensus(const PartitionInfo& partition) {
    std::cout << "Recovering Raft consensus for partition " << partition.partition_id << std::endl;
    
    // 简化实现：假设Raft节点能够自动重新建立共识
    if (raft_node_ && raft_node_->is_running()) {
        // 在实际实现中，这里应该：
        // 1. 检查Raft日志的一致性
        // 2. 重新选举领导者
        // 3. 同步日志条目
        std::cout << "Raft consensus recovery completed" << std::endl;
        return true;
    }
    
    return false;
}

bool PartitionRecoveryManager::recover_distributed_transactions(const PartitionInfo& partition) {
    std::cout << "Recovering distributed transactions for partition " << partition.partition_id << std::endl;
    
    // 简化实现：假设事务协调者能够处理恢复
    if (txn_coordinator_ && txn_coordinator_->is_running()) {
        // 在实际实现中，这里应该：
        // 1. 检查未完成的事务
        // 2. 重新执行2PC协议
        // 3. 清理孤立的事务
        std::cout << "Distributed transaction recovery completed" << std::endl;
        return true;
    }
    
    return false;
}
bool PartitionRecoveryManager::recover_data_consistency(const PartitionInfo& partition) {
    std::cout << "Recovering data consistency for partition " << partition.partition_id << std::endl;
    
    // 检测并解决数据冲突
    auto conflicts = detect_consistency_conflicts();
    
    for (const auto& conflict : conflicts) {
        std::string resolved_value;
        if (resolve_conflict_by_strategy(conflict, partition.strategy, resolved_value)) {
            resolve_consistency_conflict(conflict.key, resolved_value, partition.strategy);
        } else {
            std::cout << "Failed to resolve conflict for key " << conflict.key << std::endl;
            return false;
        }
    }
    
    std::cout << "Data consistency recovery completed" << std::endl;
    return true;
}

// 数据同步方法的简化实现
bool PartitionRecoveryManager::sync_raft_log_with_node(const std::string& node_id) {
    std::cout << "Syncing Raft log with node " << node_id << std::endl;
    // 简化实现：假设同步成功
    return true;
}

bool PartitionRecoveryManager::sync_mvcc_data_with_node(const std::string& node_id) {
    std::cout << "Syncing MVCC data with node " << node_id << std::endl;
    // 简化实现：假设同步成功
    return true;
}

bool PartitionRecoveryManager::sync_transaction_state_with_node(const std::string& node_id) {
    std::cout << "Syncing transaction state with node " << node_id << std::endl;
    // 简化实现：假设同步成功
    return true;
}

// 冲突检测方法的简化实现
std::vector<ConsistencyConflict> PartitionRecoveryManager::detect_raft_log_conflicts(const std::vector<std::string>& nodes) {
    std::vector<ConsistencyConflict> conflicts;
    // 简化实现：假设没有冲突
    return conflicts;
}

std::vector<ConsistencyConflict> PartitionRecoveryManager::detect_mvcc_data_conflicts(const std::vector<std::string>& nodes) {
    std::vector<ConsistencyConflict> conflicts;
    // 简化实现：假设没有冲突
    return conflicts;
}

bool PartitionRecoveryManager::resolve_conflict_by_strategy(const ConsistencyConflict& conflict,
                                                           PartitionRecoveryStrategy strategy,
                                                           std::string& resolved_value) {
    switch (strategy) {
        case PartitionRecoveryStrategy::MAJORITY_WINS:
            return apply_majority_wins_strategy(conflict, resolved_value);
            
        case PartitionRecoveryStrategy::LAST_WRITER_WINS:
            return apply_last_writer_wins_strategy(conflict, resolved_value);
            
        case PartitionRecoveryStrategy::MERGE_CONFLICTS:
            return apply_merge_conflicts_strategy(conflict, resolved_value);
            
        case PartitionRecoveryStrategy::MANUAL_RESOLUTION:
            std::cout << "Manual resolution required for key " << conflict.key << std::endl;
            return false;
            
        default:
            return false;
    }
}
bool PartitionRecoveryManager::apply_majority_wins_strategy(const ConsistencyConflict& conflict, std::string& resolved_value) {
    if (conflict.conflicting_values.empty()) {
        return false;
    }
    
    // 统计每个值的出现次数
    std::unordered_map<std::string, size_t> value_counts;
    for (const auto& value : conflict.conflicting_values) {
        value_counts[value]++;
    }
    
    // 找到出现次数最多的值
    std::string majority_value;
    size_t max_count = 0;
    
    for (const auto& pair : value_counts) {
        if (pair.second > max_count) {
            max_count = pair.second;
            majority_value = pair.first;
        }
    }
    
    resolved_value = majority_value;
    return true;
}

bool PartitionRecoveryManager::apply_last_writer_wins_strategy(const ConsistencyConflict& conflict, std::string& resolved_value) {
    if (conflict.conflicting_values.empty() || conflict.timestamps.empty()) {
        return false;
    }
    
    // 找到最新的时间戳对应的值
    uint64_t latest_timestamp = 0;
    size_t latest_index = 0;
    
    for (size_t i = 0; i < conflict.timestamps.size(); i++) {
        if (conflict.timestamps[i] > latest_timestamp) {
            latest_timestamp = conflict.timestamps[i];
            latest_index = i;
        }
    }
    
    if (latest_index < conflict.conflicting_values.size()) {
        resolved_value = conflict.conflicting_values[latest_index];
        return true;
    }
    
    return false;
}

bool PartitionRecoveryManager::apply_merge_conflicts_strategy(const ConsistencyConflict& conflict, std::string& resolved_value) {
    // 简化实现：合并所有值（用分隔符连接）
    if (conflict.conflicting_values.empty()) {
        return false;
    }
    
    std::ostringstream merged;
    for (size_t i = 0; i < conflict.conflicting_values.size(); i++) {
        if (i > 0) {
            merged << "|";
        }
        merged << conflict.conflicting_values[i];
    }
    
    resolved_value = merged.str();
    return true;
}

void PartitionRecoveryManager::monitor_cluster_health() {
    check_raft_leader_health();
    check_transaction_coordinator_health();
    check_data_consistency_health();
    update_statistics();
}

void PartitionRecoveryManager::check_raft_leader_health() {
    if (raft_node_ && raft_node_->is_running()) {
        if (!raft_node_->is_leader() && raft_node_->get_leader_id().empty()) {
            std::cout << "Warning: No Raft leader detected" << std::endl;
        }
    }
}

void PartitionRecoveryManager::check_transaction_coordinator_health() {
    if (txn_coordinator_ && !txn_coordinator_->is_running()) {
        std::cout << "Warning: Transaction coordinator is not running" << std::endl;
    }
}
void PartitionRecoveryManager::check_data_consistency_health() {
    // 检查是否有未解决的冲突
    auto conflicts = detect_consistency_conflicts();
    if (!conflicts.empty()) {
        std::cout << "Warning: " << conflicts.size() << " consistency conflicts detected" << std::endl;
    }
}

void PartitionRecoveryManager::set_partition_state(PartitionState state) {
    {
        std::lock_guard<std::mutex> lock(cluster_state_mutex_);
        current_partition_state_ = state;
    }
    
    notify_components_of_partition_state(state);
}

void PartitionRecoveryManager::notify_components_of_partition_state(PartitionState state) {
    // 通知Raft节点分区状态变化
    if (raft_node_) {
        // 在实际实现中，这里应该调用Raft节点的相应方法
        std::cout << "Notified Raft node of partition state: " << static_cast<int>(state) << std::endl;
    }
    
    // 通知事务协调者分区状态变化
    if (txn_coordinator_) {
        // 在实际实现中，这里应该调用事务协调者的相应方法
        std::cout << "Notified transaction coordinator of partition state: " << static_cast<int>(state) << std::endl;
    }
}

void PartitionRecoveryManager::pause_write_operations() {
    std::cout << "Pausing write operations due to partition" << std::endl;
    // 在实际实现中，这里应该暂停所有写操作
}

void PartitionRecoveryManager::resume_write_operations() {
    std::cout << "Resuming write operations after partition recovery" << std::endl;
    // 在实际实现中，这里应该恢复所有写操作
}

void PartitionRecoveryManager::update_statistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    // 计算恢复率
    if (stats_.total_partitions_detected > 0) {
        stats_.partition_recovery_rate = 
            static_cast<double>(stats_.partitions_recovered) / stats_.total_partitions_detected;
    }
    
    // 计算检测准确率
    if (stats_.false_positives + stats_.false_negatives > 0) {
        size_t total_detections = stats_.total_partitions_detected + stats_.false_positives;
        if (total_detections > 0) {
            stats_.detection_accuracy = 
                static_cast<double>(stats_.total_partitions_detected) / total_detections;
        }
    }
}

void PartitionRecoveryManager::record_partition_detection(const PartitionInfo& partition) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_partitions_detected++;
    stats_.last_partition_time = std::chrono::system_clock::now();
}

void PartitionRecoveryManager::record_partition_recovery(const std::string& recovery_id) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.partitions_recovered++;
    stats_.last_recovery_time = std::chrono::system_clock::now();
}

void PartitionRecoveryManager::cleanup_completed_recoveries() {
    std::lock_guard<std::mutex> lock(recovery_contexts_mutex_);
    
    // 保留最近的100个已完成恢复记录
    if (completed_recoveries_.size() > 100) {
        // 简化实现：清除所有已完成的恢复
        completed_recoveries_.clear();
    }
}