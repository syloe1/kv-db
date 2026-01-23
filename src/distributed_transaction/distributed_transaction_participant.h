#pragma once

#include "distributed_transaction_types.h"
#include "distributed_transaction_coordinator.h"
#include "../transaction/transaction.h"
#include "../mvcc/mvcc_manager.h"
#include <unordered_map>
#include <queue>
#include <thread>
#include <condition_variable>

// 分布式事务参与者
class DistributedTransactionParticipant {
public:
    DistributedTransactionParticipant(
        const std::string& participant_id,
        std::shared_ptr<DistributedTransactionNetworkInterface> network,
        std::shared_ptr<TransactionManager> local_txn_manager,
        std::shared_ptr<MVCCManager> mvcc_manager
    );
    
    ~DistributedTransactionParticipant();
    
    // 生命周期管理
    bool start();
    void stop();
    bool is_running() const;
    
    // 消息处理
    void handle_message(const TwoPhaseCommitMessage& message);
    
    // 协调者注册
    bool register_coordinator(const std::string& coordinator_id, 
                             const std::string& address, uint16_t port);
    bool unregister_coordinator(const std::string& coordinator_id);
    
    // 事务查询
    std::vector<std::string> get_active_transactions() const;
    bool is_transaction_active(const std::string& transaction_id) const;
    
    // 统计信息
    struct ParticipantStats {
        size_t total_transactions;
        size_t prepared_transactions;
        size_t committed_transactions;
        size_t aborted_transactions;
        size_t timeout_transactions;
        double average_prepare_time_ms;
        double average_commit_time_ms;
        size_t total_operations;
        
        ParticipantStats() : total_transactions(0), prepared_transactions(0),
                           committed_transactions(0), aborted_transactions(0),
                           timeout_transactions(0), average_prepare_time_ms(0.0),
                           average_commit_time_ms(0.0), total_operations(0) {}
    };
    
    ParticipantStats get_statistics() const;

private:
    std::string participant_id_;
    std::shared_ptr<DistributedTransactionNetworkInterface> network_;
    std::shared_ptr<TransactionManager> local_txn_manager_;
    std::shared_ptr<MVCCManager> mvcc_manager_;
    
    // 活跃的分布式事务
    mutable std::mutex transactions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<TransactionContext>> active_transactions_;
    std::unordered_map<std::string, std::vector<DistributedOperation>> transaction_operations_;
    
    // 已注册的协调者
    mutable std::mutex coordinators_mutex_;
    std::unordered_map<std::string, ParticipantInfo> registered_coordinators_;
    
    // 消息队列
    mutable std::mutex message_queue_mutex_;
    std::queue<TwoPhaseCommitMessage> message_queue_;
    std::condition_variable message_queue_cv_;
    
    // 工作线程
    std::atomic<bool> running_;
    std::thread participant_thread_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    ParticipantStats stats_;
    
    // 私有方法
    void participant_main_loop();
    
    // 2PC消息处理
    void handle_prepare_message(const TwoPhaseCommitMessage& message);
    void handle_commit_message(const TwoPhaseCommitMessage& message);
    void handle_abort_message(const TwoPhaseCommitMessage& message);
    void handle_recovery_request(const TwoPhaseCommitMessage& message);
    
    // 事务操作
    bool prepare_transaction(const std::string& transaction_id,
                           const std::vector<DistributedOperation>& operations);
    bool commit_transaction(const std::string& transaction_id);
    bool abort_transaction(const std::string& transaction_id);
    
    // 操作执行
    bool execute_operations(const std::vector<DistributedOperation>& operations,
                           std::shared_ptr<TransactionContext> context);
    bool validate_operations(const std::vector<DistributedOperation>& operations);
    
    // 响应发送
    void send_prepare_response(const std::string& transaction_id,
                              const std::string& coordinator_id,
                              bool success, const std::string& error_message = "");
    void send_commit_response(const std::string& transaction_id,
                             const std::string& coordinator_id);
    void send_abort_response(const std::string& transaction_id,
                            const std::string& coordinator_id);
    
    // 统计更新
    void update_statistics();
};