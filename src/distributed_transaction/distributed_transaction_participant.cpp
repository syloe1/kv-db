#include "distributed_transaction_participant.h"
#include <iostream>

DistributedTransactionParticipant::DistributedTransactionParticipant(
    const std::string& participant_id,
    std::shared_ptr<DistributedTransactionNetworkInterface> network,
    std::shared_ptr<TransactionManager> local_txn_manager,
    std::shared_ptr<MVCCManager> mvcc_manager)
    : participant_id_(participant_id), network_(network),
      local_txn_manager_(local_txn_manager), mvcc_manager_(mvcc_manager),
      running_(false) {
    
    std::cout << "DistributedTransactionParticipant " << participant_id_ << " initialized" << std::endl;
}

DistributedTransactionParticipant::~DistributedTransactionParticipant() {
    stop();
}

bool DistributedTransactionParticipant::start() {
    if (running_.load()) {
        return false;
    }
    
    // 设置网络消息处理回调
    network_->set_message_handler([this](const TwoPhaseCommitMessage& msg) {
        handle_message(msg);
    });
    
    // 启动网络服务
    if (!network_->start("0.0.0.0", 9091)) {
        std::cout << "Failed to start participant network" << std::endl;
        return false;
    }
    
    running_.store(true);
    
    // 启动工作线程
    participant_thread_ = std::thread(&DistributedTransactionParticipant::participant_main_loop, this);
    
    std::cout << "DistributedTransactionParticipant " << participant_id_ << " started" << std::endl;
    return true;
}

void DistributedTransactionParticipant::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 通知等待的线程
    message_queue_cv_.notify_all();
    
    // 等待线程结束
    if (participant_thread_.joinable()) {
        participant_thread_.join();
    }
    
    // 停止网络服务
    network_->stop();
    
    // 中止所有活跃事务
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        for (auto& pair : active_transactions_) {
            local_txn_manager_->abort_transaction(pair.second->get_transaction_id());
        }
        active_transactions_.clear();
    }
    
    std::cout << "DistributedTransactionParticipant " << participant_id_ << " stopped" << std::endl;
}

bool DistributedTransactionParticipant::is_running() const {
    return running_.load();
}

void DistributedTransactionParticipant::handle_message(const TwoPhaseCommitMessage& message) {
    {
        std::lock_guard<std::mutex> lock(message_queue_mutex_);
        message_queue_.push(message);
    }
    message_queue_cv_.notify_one();
}

bool DistributedTransactionParticipant::register_coordinator(
    const std::string& coordinator_id, const std::string& address, uint16_t port) {
    
    std::lock_guard<std::mutex> lock(coordinators_mutex_);
    
    ParticipantInfo coordinator(coordinator_id, address, port);
    registered_coordinators_[coordinator_id] = coordinator;
    
    // 添加到网络层
    network_->add_node(coordinator_id, address, port);
    
    std::cout << "Registered coordinator " << coordinator_id << " at " << address << ":" << port << std::endl;
    return true;
}

void DistributedTransactionParticipant::participant_main_loop() {
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
                case TwoPhaseCommitMessageType::PREPARE:
                    handle_prepare_message(message);
                    break;
                    
                case TwoPhaseCommitMessageType::COMMIT:
                    handle_commit_message(message);
                    break;
                    
                case TwoPhaseCommitMessageType::ABORT:
                    handle_abort_message(message);
                    break;
                    
                case TwoPhaseCommitMessageType::RECOVERY_REQUEST:
                    handle_recovery_request(message);
                    break;
                    
                default:
                    std::cout << "Unknown message type received by participant" << std::endl;
                    break;
            }
            
            lock.lock();
        }
    }
}

void DistributedTransactionParticipant::handle_prepare_message(const TwoPhaseCommitMessage& message) {
    std::cout << "Participant " << participant_id_ << " received PREPARE for transaction " 
              << message.transaction_id << std::endl;
    
    // 尝试准备事务
    bool success = prepare_transaction(message.transaction_id, message.operations);
    
    // 发送准备响应
    send_prepare_response(message.transaction_id, message.coordinator_id, success);
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_transactions++;
        if (success) {
            stats_.prepared_transactions++;
        }
    }
}

void DistributedTransactionParticipant::handle_commit_message(const TwoPhaseCommitMessage& message) {
    std::cout << "Participant " << participant_id_ << " received COMMIT for transaction " 
              << message.transaction_id << std::endl;
    
    // 提交事务
    bool success = commit_transaction(message.transaction_id);
    
    // 发送提交响应
    send_commit_response(message.transaction_id, message.coordinator_id);
    
    // 更新统计信息
    if (success) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.committed_transactions++;
    }
}

void DistributedTransactionParticipant::handle_abort_message(const TwoPhaseCommitMessage& message) {
    std::cout << "Participant " << participant_id_ << " received ABORT for transaction " 
              << message.transaction_id << std::endl;
    
    // 中止事务
    abort_transaction(message.transaction_id);
    
    // 发送中止响应
    send_abort_response(message.transaction_id, message.coordinator_id);
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.aborted_transactions++;
    }
}

bool DistributedTransactionParticipant::prepare_transaction(
    const std::string& transaction_id,
    const std::vector<DistributedOperation>& operations) {
    
    try {
        // 验证操作
        if (!validate_operations(operations)) {
            std::cout << "Invalid operations for transaction " << transaction_id << std::endl;
            return false;
        }
        
        // 开始本地事务
        auto local_context = local_txn_manager_->begin_transaction(IsolationLevel::READ_COMMITTED);
        if (!local_context) {
            std::cout << "Failed to begin local transaction for " << transaction_id << std::endl;
            return false;
        }
        
        // 执行操作
        if (!execute_operations(operations, local_context)) {
            std::cout << "Failed to execute operations for transaction " << transaction_id << std::endl;
            local_txn_manager_->abort_transaction(local_context->get_transaction_id());
            return false;
        }
        
        // 将事务设置为准备状态（不提交）
        local_context->set_state(TransactionState::PREPARING);
        
        // 保存事务上下文
        {
            std::lock_guard<std::mutex> lock(transactions_mutex_);
            active_transactions_[transaction_id] = local_context;
            transaction_operations_[transaction_id] = operations;
        }
        
        std::cout << "Transaction " << transaction_id << " prepared successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "Exception during prepare: " << e.what() << std::endl;
        return false;
    }
}

bool DistributedTransactionParticipant::commit_transaction(const std::string& transaction_id) {
    std::shared_ptr<TransactionContext> local_context;
    
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        auto it = active_transactions_.find(transaction_id);
        if (it == active_transactions_.end()) {
            std::cout << "Transaction " << transaction_id << " not found for commit" << std::endl;
            return false;
        }
        local_context = it->second;
    }
    
    // 提交本地事务
    bool success = local_txn_manager_->commit_transaction(local_context->get_transaction_id());
    
    // 清理
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        active_transactions_.erase(transaction_id);
        transaction_operations_.erase(transaction_id);
    }
    
    if (success) {
        std::cout << "Transaction " << transaction_id << " committed successfully" << std::endl;
    } else {
        std::cout << "Failed to commit transaction " << transaction_id << std::endl;
    }
    
    return success;
}

bool DistributedTransactionParticipant::abort_transaction(const std::string& transaction_id) {
    std::shared_ptr<TransactionContext> local_context;
    
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        auto it = active_transactions_.find(transaction_id);
        if (it == active_transactions_.end()) {
            std::cout << "Transaction " << transaction_id << " not found for abort" << std::endl;
            return false;
        }
        local_context = it->second;
    }
    
    // 中止本地事务
    bool success = local_txn_manager_->abort_transaction(local_context->get_transaction_id());
    
    // 清理
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        active_transactions_.erase(transaction_id);
        transaction_operations_.erase(transaction_id);
    }
    
    std::cout << "Transaction " << transaction_id << " aborted" << std::endl;
    return success;
}

bool DistributedTransactionParticipant::execute_operations(
    const std::vector<DistributedOperation>& operations,
    std::shared_ptr<TransactionContext> context) {
    
    for (const auto& op : operations) {
        try {
            if (op.operation_type == "READ") {
                // 执行读操作
                std::string value;
                if (mvcc_manager_->read(op.key, context->get_start_timestamp(), value)) {
                    context->add_read_set(op.key, context->get_start_timestamp());
                    std::cout << "Read " << op.key << " = " << value << std::endl;
                } else {
                    std::cout << "Key " << op.key << " not found" << std::endl;
                }
                
            } else if (op.operation_type == "WRITE") {
                // 执行写操作
                if (mvcc_manager_->write(op.key, op.value, 
                                       context->get_transaction_id(),
                                       context->get_start_timestamp())) {
                    context->add_write_set(op.key, op.value);
                    std::cout << "Write " << op.key << " = " << op.value << std::endl;
                } else {
                    std::cout << "Failed to write " << op.key << std::endl;
                    return false;
                }
                
            } else if (op.operation_type == "DELETE") {
                // 执行删除操作
                if (mvcc_manager_->remove(op.key, context->get_transaction_id(),
                                        context->get_start_timestamp())) {
                    std::cout << "Delete " << op.key << std::endl;
                } else {
                    std::cout << "Failed to delete " << op.key << std::endl;
                    return false;
                }
                
            } else {
                std::cout << "Unknown operation type: " << op.operation_type << std::endl;
                return false;
            }
            
        } catch (const std::exception& e) {
            std::cout << "Exception executing operation: " << e.what() << std::endl;
            return false;
        }
    }
    
    return true;
}

bool DistributedTransactionParticipant::validate_operations(
    const std::vector<DistributedOperation>& operations) {
    
    for (const auto& op : operations) {
        // 检查操作是否针对本节点
        if (op.node_id != participant_id_) {
            std::cout << "Operation for wrong node: " << op.node_id 
                      << " (expected: " << participant_id_ << ")" << std::endl;
            return false;
        }
        
        // 检查操作类型
        if (op.operation_type != "READ" && op.operation_type != "WRITE" && op.operation_type != "DELETE") {
            std::cout << "Invalid operation type: " << op.operation_type << std::endl;
            return false;
        }
        
        // 检查键
        if (op.key.empty()) {
            std::cout << "Empty key in operation" << std::endl;
            return false;
        }
        
        // 对于写操作，检查值
        if (op.operation_type == "WRITE" && op.value.empty()) {
            std::cout << "Empty value for write operation" << std::endl;
            return false;
        }
    }
    
    return true;
}

void DistributedTransactionParticipant::send_prepare_response(
    const std::string& transaction_id,
    const std::string& coordinator_id,
    bool success, const std::string& error_message) {
    
    TwoPhaseCommitMessage response;
    response.type = success ? TwoPhaseCommitMessageType::PREPARE_OK 
                           : TwoPhaseCommitMessageType::PREPARE_ABORT;
    response.transaction_id = transaction_id;
    response.coordinator_id = coordinator_id;
    response.participant_id = participant_id_;
    response.error_message = error_message;
    response.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    network_->send_message(coordinator_id, response);
    
    std::cout << "Sent prepare response (" << (success ? "OK" : "ABORT") 
              << ") for transaction " << transaction_id << std::endl;
}

void DistributedTransactionParticipant::send_commit_response(
    const std::string& transaction_id,
    const std::string& coordinator_id) {
    
    TwoPhaseCommitMessage response;
    response.type = TwoPhaseCommitMessageType::COMMIT_OK;
    response.transaction_id = transaction_id;
    response.coordinator_id = coordinator_id;
    response.participant_id = participant_id_;
    response.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    network_->send_message(coordinator_id, response);
    
    std::cout << "Sent commit response for transaction " << transaction_id << std::endl;
}

void DistributedTransactionParticipant::send_abort_response(
    const std::string& transaction_id,
    const std::string& coordinator_id) {
    
    TwoPhaseCommitMessage response;
    response.type = TwoPhaseCommitMessageType::ABORT_OK;
    response.transaction_id = transaction_id;
    response.coordinator_id = coordinator_id;
    response.participant_id = participant_id_;
    response.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    network_->send_message(coordinator_id, response);
    
    std::cout << "Sent abort response for transaction " << transaction_id << std::endl;
}

std::vector<std::string> DistributedTransactionParticipant::get_active_transactions() const {
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    
    std::vector<std::string> result;
    for (const auto& pair : active_transactions_) {
        result.push_back(pair.first);
    }
    
    return result;
}

bool DistributedTransactionParticipant::is_transaction_active(const std::string& transaction_id) const {
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    return active_transactions_.find(transaction_id) != active_transactions_.end();
}

DistributedTransactionParticipant::ParticipantStats DistributedTransactionParticipant::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void DistributedTransactionParticipant::handle_recovery_request(const TwoPhaseCommitMessage& message) {
    // 简化的恢复处理
    std::cout << "Received recovery request for transaction " << message.transaction_id << std::endl;
}