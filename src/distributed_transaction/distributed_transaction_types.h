#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>

// 分布式事务状态
enum class DistributedTransactionState {
    ACTIVE,         // 活跃状态
    PREPARING,      // 准备阶段
    PREPARED,       // 已准备
    COMMITTING,     // 提交阶段
    COMMITTED,      // 已提交
    ABORTING,       // 中止阶段
    ABORTED,        // 已中止
    UNKNOWN         // 未知状态
};

// 分布式事务参与者状态
enum class ParticipantState {
    ACTIVE,         // 活跃
    PREPARED,       // 已准备
    COMMITTED,      // 已提交
    ABORTED,        // 已中止
    TIMEOUT,        // 超时
    FAILED          // 失败
};

// 2PC消息类型
enum class TwoPhaseCommitMessageType {
    PREPARE,            // 准备消息
    PREPARE_OK,         // 准备成功
    PREPARE_ABORT,      // 准备失败
    COMMIT,             // 提交消息
    COMMIT_OK,          // 提交成功
    ABORT,              // 中止消息
    ABORT_OK,           // 中止成功
    HEARTBEAT,          // 心跳消息
    RECOVERY_REQUEST,   // 恢复请求
    RECOVERY_RESPONSE   // 恢复响应
};

// 分布式事务操作
struct DistributedOperation {
    std::string node_id;        // 目标节点ID
    std::string operation_type; // 操作类型 (READ, WRITE, DELETE)
    std::string key;            // 键
    std::string value;          // 值
    std::string condition;      // 条件（可选）
    uint64_t sequence_number;   // 序列号
    
    DistributedOperation() : sequence_number(0) {}
    
    DistributedOperation(const std::string& node, const std::string& op_type,
                        const std::string& k, const std::string& v = "")
        : node_id(node), operation_type(op_type), key(k), value(v), sequence_number(0) {}
};

// 2PC消息
struct TwoPhaseCommitMessage {
    TwoPhaseCommitMessageType type;
    std::string transaction_id;     // 全局事务ID
    std::string coordinator_id;     // 协调者ID
    std::string participant_id;     // 参与者ID
    std::vector<DistributedOperation> operations; // 操作列表
    std::string error_message;      // 错误信息
    uint64_t timestamp;             // 时间戳
    uint64_t timeout_ms;            // 超时时间（毫秒）
    
    TwoPhaseCommitMessage() : type(TwoPhaseCommitMessageType::PREPARE), 
                             timestamp(0), timeout_ms(30000) {}
    
    // 序列化方法
    std::vector<uint8_t> serialize() const;
    static TwoPhaseCommitMessage deserialize(const std::vector<uint8_t>& data);
};

// 分布式事务参与者信息
struct ParticipantInfo {
    std::string node_id;                    // 节点ID
    std::string address;                    // 网络地址
    uint16_t port;                          // 端口
    ParticipantState state;                 // 状态
    std::chrono::system_clock::time_point last_contact; // 最后联系时间
    std::vector<DistributedOperation> operations; // 该参与者的操作
    std::string prepare_response;           // 准备阶段响应
    
    ParticipantInfo() : port(0), state(ParticipantState::ACTIVE) {}
    
    ParticipantInfo(const std::string& id, const std::string& addr, uint16_t p)
        : node_id(id), address(addr), port(p), state(ParticipantState::ACTIVE),
          last_contact(std::chrono::system_clock::now()) {}
    
    bool is_responsive() const {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_contact);
        return elapsed.count() < 30; // 30秒内有响应
    }
};

// 分布式事务上下文
class DistributedTransactionContext {
public:
    DistributedTransactionContext(const std::string& txn_id, const std::string& coordinator_id);
    ~DistributedTransactionContext();
    
    // 基本属性
    const std::string& get_transaction_id() const { return transaction_id_; }
    const std::string& get_coordinator_id() const { return coordinator_id_; }
    DistributedTransactionState get_state() const;
    void set_state(DistributedTransactionState state);
    
    // 参与者管理
    void add_participant(const ParticipantInfo& participant);
    void update_participant_state(const std::string& node_id, ParticipantState state);
    std::vector<ParticipantInfo> get_participants() const;
    ParticipantInfo* get_participant(const std::string& node_id);
    size_t get_participant_count() const;
    
    // 操作管理
    void add_operation(const DistributedOperation& operation);
    std::vector<DistributedOperation> get_operations() const;
    std::vector<DistributedOperation> get_operations_for_node(const std::string& node_id) const;
    
    // 状态查询
    bool all_participants_prepared() const;
    bool any_participant_aborted() const;
    bool all_participants_committed() const;
    size_t get_prepared_count() const;
    size_t get_committed_count() const;
    size_t get_aborted_count() const;
    
    // 时间管理
    std::chrono::system_clock::time_point get_start_time() const { return start_time_; }
    std::chrono::system_clock::time_point get_prepare_time() const { return prepare_time_; }
    std::chrono::system_clock::time_point get_commit_time() const { return commit_time_; }
    void set_prepare_time(std::chrono::system_clock::time_point time) { prepare_time_ = time; }
    void set_commit_time(std::chrono::system_clock::time_point time) { commit_time_ = time; }
    
    // 超时管理
    void set_timeout(std::chrono::milliseconds timeout) { timeout_ = timeout; }
    std::chrono::milliseconds get_timeout() const { return timeout_; }
    bool is_timeout() const;
    
    // 错误处理
    void set_error(const std::string& error) { error_message_ = error; }
    std::string get_error() const { return error_message_; }
    bool has_error() const { return !error_message_.empty(); }

private:
    std::string transaction_id_;
    std::string coordinator_id_;
    DistributedTransactionState state_;
    
    std::vector<ParticipantInfo> participants_;
    std::vector<DistributedOperation> operations_;
    
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point prepare_time_;
    std::chrono::system_clock::time_point commit_time_;
    std::chrono::milliseconds timeout_;
    
    std::string error_message_;
    
    mutable std::mutex context_mutex_;
};

// 分布式事务统计信息
struct DistributedTransactionStats {
    size_t total_transactions;          // 总事务数
    size_t active_transactions;         // 活跃事务数
    size_t committed_transactions;      // 已提交事务数
    size_t aborted_transactions;        // 已中止事务数
    size_t timeout_transactions;        // 超时事务数
    
    double average_prepare_time_ms;     // 平均准备时间
    double average_commit_time_ms;      // 平均提交时间
    double success_rate;                // 成功率
    
    size_t total_participants;          // 总参与者数
    size_t average_participants_per_txn; // 平均每事务参与者数
    
    uint64_t total_operations;          // 总操作数
    uint64_t read_operations;           // 读操作数
    uint64_t write_operations;          // 写操作数
    uint64_t delete_operations;         // 删除操作数
    
    DistributedTransactionStats() : total_transactions(0), active_transactions(0),
                                   committed_transactions(0), aborted_transactions(0),
                                   timeout_transactions(0), average_prepare_time_ms(0.0),
                                   average_commit_time_ms(0.0), success_rate(0.0),
                                   total_participants(0), average_participants_per_txn(0),
                                   total_operations(0), read_operations(0),
                                   write_operations(0), delete_operations(0) {}
};

// 分布式事务配置
struct DistributedTransactionConfig {
    std::chrono::milliseconds default_timeout{30000};      // 默认超时时间
    std::chrono::milliseconds prepare_timeout{10000};      // 准备阶段超时
    std::chrono::milliseconds commit_timeout{10000};       // 提交阶段超时
    std::chrono::milliseconds heartbeat_interval{5000};    // 心跳间隔
    std::chrono::milliseconds recovery_interval{60000};    // 恢复检查间隔
    
    size_t max_retry_attempts{3};                          // 最大重试次数
    size_t max_concurrent_transactions{1000};              // 最大并发事务数
    
    bool enable_recovery{true};                             // 启用恢复机制
    bool enable_heartbeat{true};                            // 启用心跳机制
    bool enable_logging{true};                              // 启用日志记录
    
    std::string log_directory{"./distributed_txn_logs"};   // 日志目录
    
    DistributedTransactionConfig() = default;
    
    bool is_valid() const {
        return default_timeout.count() > 0 &&
               prepare_timeout.count() > 0 &&
               commit_timeout.count() > 0 &&
               max_retry_attempts > 0 &&
               max_concurrent_transactions > 0;
    }
};