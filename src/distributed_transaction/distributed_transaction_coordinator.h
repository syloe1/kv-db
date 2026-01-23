#pragma once

#include "distributed_transaction_types.h"
#include "../transaction/transaction.h"
#include <unordered_map>
#include <queue>
#include <thread>
#include <condition_variable>
#include <functional>

// 前向声明
class DistributedTransactionNetworkInterface;

// 分布式事务协调者
class DistributedTransactionCoordinator {
public:
    DistributedTransactionCoordinator(
        const std::string& coordinator_id,
        const DistributedTransactionConfig& config,
        std::shared_ptr<DistributedTransactionNetworkInterface> network,
        std::shared_ptr<TransactionManager> local_txn_manager
    );
    
    ~DistributedTransactionCoordinator();
    
    // 生命周期管理
    bool start();
    void stop();
    bool is_running() const;
    
    // 分布式事务接口
    std::string begin_distributed_transaction(
        const std::vector<DistributedOperation>& operations,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)
    );
    
    bool commit_distributed_transaction(const std::string& transaction_id);
    bool abort_distributed_transaction(const std::string& transaction_id);
    
    // 事务查询
    std::shared_ptr<DistributedTransactionContext> get_transaction(const std::string& transaction_id);
    std::vector<std::string> get_active_transactions() const;
    DistributedTransactionState get_transaction_state(const std::string& transaction_id) const;
    
    // 消息处理
    void handle_message(const TwoPhaseCommitMessage& message);
    
    // 参与者管理
    bool register_participant(const std::string& node_id, const std::string& address, uint16_t port);
    bool unregister_participant(const std::string& node_id);
    std::vector<ParticipantInfo> get_registered_participants() const;
    
    // 统计信息
    DistributedTransactionStats get_statistics() const;
    void print_statistics() const;
    
    // 配置管理
    void update_config(const DistributedTransactionConfig& config);
    DistributedTransactionConfig get_config() const;

private:
    std::string coordinator_id_;
    DistributedTransactionConfig config_;
    std::shared_ptr<DistributedTransactionNetworkInterface> network_;
    std::shared_ptr<TransactionManager> local_txn_manager_;
    
    // 事务管理
    mutable std::mutex transactions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<DistributedTransactionContext>> active_transactions_;
    std::unordered_map<std::string, std::shared_ptr<DistributedTransactionContext>> completed_transactions_;
    
    // 参与者注册表
    mutable std::mutex participants_mutex_;
    std::unordered_map<std::string, ParticipantInfo> registered_participants_;
    
    // 消息队列
    mutable std::mutex message_queue_mutex_;
    std::queue<TwoPhaseCommitMessage> message_queue_;
    std::condition_variable message_queue_cv_;
    
    // 工作线程
    std::atomic<bool> running_;
    std::thread coordinator_thread_;
    std::thread timeout_checker_thread_;
    std::thread recovery_thread_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    DistributedTransactionStats stats_;
    
    // 事务ID生成
    std::atomic<uint64_t> next_transaction_id_;
    
    // 私有方法
    std::string generate_transaction_id();
    void coordinator_main_loop();
    void timeout_checker_loop();
    void recovery_loop();
    
    // 2PC协议实现
    bool execute_prepare_phase(std::shared_ptr<DistributedTransactionContext> context);
    bool execute_commit_phase(std::shared_ptr<DistributedTransactionContext> context);
    bool execute_abort_phase(std::shared_ptr<DistributedTransactionContext> context);
    
    // 消息处理
    void handle_prepare_response(const TwoPhaseCommitMessage& message);
    void handle_commit_response(const TwoPhaseCommitMessage& message);
    void handle_abort_response(const TwoPhaseCommitMessage& message);
    void handle_recovery_request(const TwoPhaseCommitMessage& message);
    
    // 消息发送
    bool send_prepare_message(std::shared_ptr<DistributedTransactionContext> context,
                             const std::string& participant_id);
    bool send_commit_message(std::shared_ptr<DistributedTransactionContext> context,
                            const std::string& participant_id);
    bool send_abort_message(std::shared_ptr<DistributedTransactionContext> context,
                           const std::string& participant_id);
    
    // 超时处理
    void check_transaction_timeouts();
    void handle_transaction_timeout(std::shared_ptr<DistributedTransactionContext> context);
    
    // 恢复机制
    void perform_recovery();
    void recover_transaction(std::shared_ptr<DistributedTransactionContext> context);
    
    // 统计更新
    void update_statistics();
    void record_transaction_completion(std::shared_ptr<DistributedTransactionContext> context);
    
    // 工具方法
    std::vector<std::string> get_participants_for_operations(
        const std::vector<DistributedOperation>& operations) const;
    bool validate_operations(const std::vector<DistributedOperation>& operations) const;
    void cleanup_completed_transactions();
};

// 分布式事务网络接口
class DistributedTransactionNetworkInterface {
public:
    virtual ~DistributedTransactionNetworkInterface() = default;
    
    // 消息发送
    virtual bool send_message(const std::string& target_node, 
                             const TwoPhaseCommitMessage& message) = 0;
    
    // 广播消息
    virtual bool broadcast_message(const std::vector<std::string>& target_nodes,
                                  const TwoPhaseCommitMessage& message) = 0;
    
    // 消息处理回调
    virtual void set_message_handler(
        std::function<void(const TwoPhaseCommitMessage&)> handler) = 0;
    
    // 网络管理
    virtual bool start(const std::string& address, uint16_t port) = 0;
    virtual void stop() = 0;
    
    // 节点管理
    virtual bool add_node(const std::string& node_id, const std::string& address, uint16_t port) = 0;
    virtual bool remove_node(const std::string& node_id) = 0;
    virtual bool is_node_reachable(const std::string& node_id) = 0;
    virtual std::vector<std::string> get_reachable_nodes() = 0;
};