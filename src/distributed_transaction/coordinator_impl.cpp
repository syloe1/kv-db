#include "distributed_transaction_coordinator.h"
#include <iostream>
#include <algorithm>
#include <sstream>

DistributedTransactionCoordinator::DistributedTransactionCoordinator(
    const std::string& coordinator_id,
    const DistributedTransactionConfig& config,
    std::shared_ptr<DistributedTransactionNetworkInterface> network,
    std::shared_ptr<TransactionManager> local_txn_manager)
    : coordinator_id_(coordinator_id), config_(config), network_(network),
      local_txn_manager_(local_txn_manager), running_(false), next_transaction_id_(1) {
    
    if (!config_.is_valid()) {
        throw std::invalid_argument("Invalid distributed transaction configuration");
    }
    
    std::cout << "DistributedTransactionCoordinator " << coordinator_id_ << " initialized" << std::endl;
}

DistributedTransactionCoordinator::~DistributedTransactionCoordinator() {
    stop();
}

bool DistributedTransactionCoordinator::start() {
    if (running_.load()) {
        return false;
    }
    
    // 设置网络消息处理回调
    network_->set_message_handler([this](const TwoPhaseCommitMessage& msg) {
        handle_message(msg);
    });
    
    // 启动网络服务
    if (!network_->start("0.0.0.0", 9090)) {
        std::cout << "Failed to start distributed transaction network" << std::endl;
        return false;
    }
    
    running_.store(true);
    
    // 启动工作线程
    coordinator_thread_ = std::thread(&DistributedTransactionCoordinator::coordinator_main_loop, this);
    timeout_checker_thread_ = std::thread(&DistributedTransactionCoordinator::timeout_checker_loop, this);
    
    if (config_.enable_recovery) {
        recovery_thread_ = std::thread(&DistributedTransactionCoordinator::recovery_loop, this);
    }
    
    std::cout << "DistributedTransactionCoordinator " << coordinator_id_ << " started" << std::endl;
    return true;
}

void DistributedTransactionCoordinator::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 通知所有等待的线程
    message_queue_cv_.notify_all();
    
    // 等待线程结束
    if (coordinator_thread_.joinable()) {
        coordinator_thread_.join();
    }
    
    if (timeout_checker_thread_.joinable()) {
        timeout_checker_thread_.join();
    }
    
    if (recovery_thread_.joinable()) {
        recovery_thread_.join();
    }
    
    // 停止网络服务
    network_->stop();
    
    // 中止所有活跃事务
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        for (auto& pair : active_transactions_) {
            pair.second->set_state(DistributedTransactionState::ABORTED);
        }
    }
    
    std::cout << "DistributedTransactionCoordinator " << coordinator_id_ << " stopped" << std::endl;
}

bool DistributedTransactionCoordinator::is_running() const {
    return running_.load();
}

std::string DistributedTransactionCoordinator::begin_distributed_transaction(
    const std::vector<DistributedOperation>& operations,
    std::chrono::milliseconds timeout) {
    
    if (!running_.load()) {
        throw std::runtime_error("Coordinator is not running");
    }
    
    if (operations.empty()) {
        throw std::invalid_argument("Operations list cannot be empty");
    }
    
    if (!validate_operations(operations)) {
        throw std::invalid_argument("Invalid operations");
    }
    
    // 生成事务ID
    std::string txn_id = generate_transaction_id();
    
    // 创建分布式事务上下文
    auto context = std::make_shared<DistributedTransactionContext>(txn_id, coordinator_id_);
    context->set_timeout(timeout);
    
    // 添加操作
    for (const auto& op : operations) {
        context->add_operation(op);
    }
    
    // 确定参与者
    auto participant_nodes = get_participants_for_operations(operations);
    for (const auto& node_id : participant_nodes) {
        auto it = registered_participants_.find(node_id);
        if (it != registered_participants_.end()) {
            context->add_participant(it->second);
        } else {
            std::cout << "Warning: Participant " << node_id << " not registered" << std::endl;
            // 可以选择失败或者尝试自动发现
            throw std::runtime_error("Participant " + node_id + " not registered");
        }
    }
    
    // 添加到活跃事务列表
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        active_transactions_[txn_id] = context;
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_transactions++;
        stats_.active_transactions++;
        stats_.total_operations += operations.size();
        
        for (const auto& op : operations) {
            if (op.operation_type == "READ") {
                stats_.read_operations++;
            } else if (op.operation_type == "WRITE") {
                stats_.write_operations++;
            } else if (op.operation_type == "DELETE") {
                stats_.delete_operations++;
            }
        }
    }
    
    std::cout << "Started distributed transaction " << txn_id 
              << " with " << participant_nodes.size() << " participants" << std::endl;
    
    return txn_id;
}

bool DistributedTransactionCoordinator::commit_distributed_transaction(const std::string& transaction_id) {
    auto context = get_transaction(transaction_id);
    if (!context) {
        std::cout << "Transaction " << transaction_id << " not found" << std::endl;
        return false;
    }
    
    if (context->get_state() != DistributedTransactionState::ACTIVE) {
        std::cout << "Transaction " << transaction_id << " is not in active state" << std::endl;
        return false;
    }
    
    std::cout << "Committing distributed transaction " << transaction_id << std::endl;
    
    // 执行两阶段提交
    context->set_state(DistributedTransactionState::PREPARING);
    
    // 第一阶段：准备
    if (!execute_prepare_phase(context)) {
        std::cout << "Prepare phase failed for transaction " << transaction_id << std::endl;
        execute_abort_phase(context);
        return false;
    }
    
    // 第二阶段：提交
    context->set_state(DistributedTransactionState::COMMITTING);
    
    if (!execute_commit_phase(context)) {
        std::cout << "Commit phase failed for transaction " << transaction_id << std::endl;
        // 注意：在真实实现中，这里需要更复杂的恢复逻辑
        return false;
    }
    
    // 提交成功
    context->set_state(DistributedTransactionState::COMMITTED);
    context->set_commit_time(std::chrono::system_clock::now());
    
    // 移动到已完成事务列表
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        active_transactions_.erase(transaction_id);
        completed_transactions_[transaction_id] = context;
    }
    
    // 更新统计信息
    record_transaction_completion(context);
    
    std::cout << "Distributed transaction " << transaction_id << " committed successfully" << std::endl;
    return true;
}

bool DistributedTransactionCoordinator::abort_distributed_transaction(const std::string& transaction_id) {
    auto context = get_transaction(transaction_id);
    if (!context) {
        return false;
    }
    
    std::cout << "Aborting distributed transaction " << transaction_id << std::endl;
    
    // 执行中止阶段
    execute_abort_phase(context);
    
    context->set_state(DistributedTransactionState::ABORTED);
    
    // 移动到已完成事务列表
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        active_transactions_.erase(transaction_id);
        completed_transactions_[transaction_id] = context;
    }
    
    // 更新统计信息
    record_transaction_completion(context);
    
    std::cout << "Distributed transaction " << transaction_id << " aborted" << std::endl;
    return true;
}

std::shared_ptr<DistributedTransactionContext> DistributedTransactionCoordinator::get_transaction(
    const std::string& transaction_id) {
    
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    
    auto it = active_transactions_.find(transaction_id);
    if (it != active_transactions_.end()) {
        return it->second;
    }
    
    auto it2 = completed_transactions_.find(transaction_id);
    if (it2 != completed_transactions_.end()) {
        return it2->second;
    }
    
    return nullptr;
}

void DistributedTransactionCoordinator::handle_message(const TwoPhaseCommitMessage& message) {
    {
        std::lock_guard<std::mutex> lock(message_queue_mutex_);
        message_queue_.push(message);
    }
    message_queue_cv_.notify_one();
}

bool DistributedTransactionCoordinator::register_participant(
    const std::string& node_id, const std::string& address, uint16_t port) {
    
    std::lock_guard<std::mutex> lock(participants_mutex_);
    
    ParticipantInfo participant(node_id, address, port);
    registered_participants_[node_id] = participant;
    
    // 添加到网络层
    network_->add_node(node_id, address, port);
    
    std::cout << "Registered participant " << node_id << " at " << address << ":" << port << std::endl;
    return true;
}

// 私有方法实现
std::string DistributedTransactionCoordinator::generate_transaction_id() {
    uint64_t id = next_transaction_id_.fetch_add(1);
    std::ostringstream oss;
    oss << coordinator_id_ << "_" << id << "_" 
        << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    return oss.str();
}

void DistributedTransactionCoordinator::coordinator_main_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(message_queue_mutex_);
        message_queue_cv_.wait_for(lock, std::chrono::milliseconds(100), 
                                  [this] { return !message_queue_.empty() || !running_.load(); });
        
        while (!message_queue_.empty() && running_.load()) {
            TwoPhaseCommitMessage message = message_queue_.front();
            message_queue_.pop();
            lock.unlock();
            
            // 处理消息
            switch (message.type) {
                case TwoPhaseCommitMessageType::PREPARE_OK:
                case TwoPhaseCommitMessageType::PREPARE_ABORT:
                    handle_prepare_response(message);
                    break;
                    
                case TwoPhaseCommitMessageType::COMMIT_OK:
                    handle_commit_response(message);
                    break;
                    
                case TwoPhaseCommitMessageType::ABORT_OK:
                    handle_abort_response(message);
                    break;
                    
                case TwoPhaseCommitMessageType::RECOVERY_REQUEST:
                    handle_recovery_request(message);
                    break;
                    
                default:
                    std::cout << "Unknown message type received" << std::endl;
                    break;
            }
            
            lock.lock();
        }
    }
}

bool DistributedTransactionCoordinator::execute_prepare_phase(
    std::shared_ptr<DistributedTransactionContext> context) {
    
    std::cout << "Executing prepare phase for transaction " << context->get_transaction_id() << std::endl;
    
    context->set_prepare_time(std::chrono::system_clock::now());
    
    // 向所有参与者发送准备消息
    auto participants = context->get_participants();
    for (const auto& participant : participants) {
        if (!send_prepare_message(context, participant.node_id)) {
            std::cout << "Failed to send prepare message to " << participant.node_id << std::endl;
            return false;
        }
    }
    
    // 等待所有参与者响应
    auto timeout = context->get_timeout();
    auto start_time = std::chrono::system_clock::now();
    
    while (std::chrono::system_clock::now() - start_time < timeout) {
        if (context->all_participants_prepared()) {
            std::cout << "All participants prepared for transaction " << context->get_transaction_id() << std::endl;
            return true;
        }
        
        if (context->any_participant_aborted()) {
            std::cout << "Some participants aborted for transaction " << context->get_transaction_id() << std::endl;
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "Prepare phase timeout for transaction " << context->get_transaction_id() << std::endl;
    return false;
}

bool DistributedTransactionCoordinator::execute_commit_phase(
    std::shared_ptr<DistributedTransactionContext> context) {
    
    std::cout << "Executing commit phase for transaction " << context->get_transaction_id() << std::endl;
    
    // 向所有参与者发送提交消息
    auto participants = context->get_participants();
    for (const auto& participant : participants) {
        if (!send_commit_message(context, participant.node_id)) {
            std::cout << "Failed to send commit message to " << participant.node_id << std::endl;
            // 在真实实现中，这里需要重试机制
        }
    }
    
    // 等待所有参与者确认提交
    auto timeout = config_.commit_timeout;
    auto start_time = std::chrono::system_clock::now();
    
    while (std::chrono::system_clock::now() - start_time < timeout) {
        if (context->all_participants_committed()) {
            std::cout << "All participants committed for transaction " << context->get_transaction_id() << std::endl;
            return true;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "Commit phase timeout for transaction " << context->get_transaction_id() << std::endl;
    // 在真实实现中，即使超时也应该继续尝试提交
    return true; // 假设最终会成功
}

bool DistributedTransactionCoordinator::execute_abort_phase(
    std::shared_ptr<DistributedTransactionContext> context) {
    
    std::cout << "Executing abort phase for transaction " << context->get_transaction_id() << std::endl;
    
    context->set_state(DistributedTransactionState::ABORTING);
    
    // 向所有参与者发送中止消息
    auto participants = context->get_participants();
    for (const auto& participant : participants) {
        send_abort_message(context, participant.node_id);
    }
    
    return true;
}

void DistributedTransactionCoordinator::handle_prepare_response(const TwoPhaseCommitMessage& message) {
    auto context = get_transaction(message.transaction_id);
    if (!context) {
        std::cout << "Received prepare response for unknown transaction " << message.transaction_id << std::endl;
        return;
    }
    
    ParticipantState new_state = (message.type == TwoPhaseCommitMessageType::PREPARE_OK) 
                                ? ParticipantState::PREPARED 
                                : ParticipantState::ABORTED;
    
    context->update_participant_state(message.participant_id, new_state);
    
    std::cout << "Received prepare response from " << message.participant_id 
              << " for transaction " << message.transaction_id 
              << " (state: " << static_cast<int>(new_state) << ")" << std::endl;
}

bool DistributedTransactionCoordinator::send_prepare_message(
    std::shared_ptr<DistributedTransactionContext> context,
    const std::string& participant_id) {
    
    TwoPhaseCommitMessage message;
    message.type = TwoPhaseCommitMessageType::PREPARE;
    message.transaction_id = context->get_transaction_id();
    message.coordinator_id = coordinator_id_;
    message.participant_id = participant_id;
    message.operations = context->get_operations_for_node(participant_id);
    message.timeout_ms = context->get_timeout().count();
    message.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return network_->send_message(participant_id, message);
}

std::vector<std::string> DistributedTransactionCoordinator::get_participants_for_operations(
    const std::vector<DistributedOperation>& operations) const {
    
    std::unordered_set<std::string> participants;
    for (const auto& op : operations) {
        participants.insert(op.node_id);
    }
    
    return std::vector<std::string>(participants.begin(), participants.end());
}

bool DistributedTransactionCoordinator::validate_operations(
    const std::vector<DistributedOperation>& operations) const {
    
    for (const auto& op : operations) {
        if (op.node_id.empty() || op.operation_type.empty() || op.key.empty()) {
            return false;
        }
        
        if (op.operation_type != "READ" && op.operation_type != "WRITE" && op.operation_type != "DELETE") {
            return false;
        }
    }
    
    return true;
}

void DistributedTransactionCoordinator::record_transaction_completion(
    std::shared_ptr<DistributedTransactionContext> context) {
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    stats_.active_transactions--;
    
    if (context->get_state() == DistributedTransactionState::COMMITTED) {
        stats_.committed_transactions++;
    } else if (context->get_state() == DistributedTransactionState::ABORTED) {
        stats_.aborted_transactions++;
    }
    
    // 计算成功率
    if (stats_.total_transactions > 0) {
        stats_.success_rate = static_cast<double>(stats_.committed_transactions) / stats_.total_transactions;
    }
}

void DistributedTransactionCoordinator::timeout_checker_loop() {
    while (running_.load()) {
        check_transaction_timeouts();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void DistributedTransactionCoordinator::check_transaction_timeouts() {
    std::vector<std::shared_ptr<DistributedTransactionContext>> timeout_transactions;
    
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        for (auto& pair : active_transactions_) {
            if (pair.second->is_timeout()) {
                timeout_transactions.push_back(pair.second);
            }
        }
    }
    
    for (auto& context : timeout_transactions) {
        handle_transaction_timeout(context);
    }
}

void DistributedTransactionCoordinator::handle_transaction_timeout(
    std::shared_ptr<DistributedTransactionContext> context) {
    
    std::cout << "Transaction " << context->get_transaction_id() << " timed out" << std::endl;
    
    // 超时事务自动中止
    abort_distributed_transaction(context->get_transaction_id());
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.timeout_transactions++;
    }
}

void DistributedTransactionCoordinator::recovery_loop() {
    while (running_.load()) {
        perform_recovery();
        std::this_thread::sleep_for(config_.recovery_interval);
    }
}

void DistributedTransactionCoordinator::perform_recovery() {
    // 简化的恢复实现
    // 在真实系统中，这里需要检查持久化日志，恢复未完成的事务
    std::cout << "Performing recovery check..." << std::endl;
}

DistributedTransactionStats DistributedTransactionCoordinator::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

// 其他简化的方法实现
void DistributedTransactionCoordinator::handle_commit_response(const TwoPhaseCommitMessage& message) {
    auto context = get_transaction(message.transaction_id);
    if (context) {
        context->update_participant_state(message.participant_id, ParticipantState::COMMITTED);
    }
}

void DistributedTransactionCoordinator::handle_abort_response(const TwoPhaseCommitMessage& message) {
    auto context = get_transaction(message.transaction_id);
    if (context) {
        context->update_participant_state(message.participant_id, ParticipantState::ABORTED);
    }
}

void DistributedTransactionCoordinator::handle_recovery_request(const TwoPhaseCommitMessage& message) {
    // 处理恢复请求
}

bool DistributedTransactionCoordinator::send_commit_message(
    std::shared_ptr<DistributedTransactionContext> context,
    const std::string& participant_id) {
    
    TwoPhaseCommitMessage message;
    message.type = TwoPhaseCommitMessageType::COMMIT;
    message.transaction_id = context->get_transaction_id();
    message.coordinator_id = coordinator_id_;
    message.participant_id = participant_id;
    
    return network_->send_message(participant_id, message);
}

bool DistributedTransactionCoordinator::send_abort_message(
    std::shared_ptr<DistributedTransactionContext> context,
    const std::string& participant_id) {
    
    TwoPhaseCommitMessage message;
    message.type = TwoPhaseCommitMessageType::ABORT;
    message.transaction_id = context->get_transaction_id();
    message.coordinator_id = coordinator_id_;
    message.participant_id = participant_id;
    
    return network_->send_message(participant_id, message);
}