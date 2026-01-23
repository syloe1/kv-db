#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <atomic>

// 事务状态
enum class TransactionState {
    ACTIVE,     // 活跃状态
    PREPARING,  // 准备提交
    COMMITTED,  // 已提交
    ABORTED,    // 已中止
    UNKNOWN     // 未知状态
};

// 事务隔离级别
enum class IsolationLevel {
    READ_UNCOMMITTED,  // 读未提交
    READ_COMMITTED,    // 读已提交
    REPEATABLE_READ,   // 可重复读
    SERIALIZABLE       // 串行化
};

// 锁类型
enum class LockType {
    SHARED,     // 共享锁（读锁）
    EXCLUSIVE,  // 排他锁（写锁）
    INTENTION_SHARED,   // 意向共享锁
    INTENTION_EXCLUSIVE // 意向排他锁
};

// 事务操作类型
enum class OperationType {
    READ,
    WRITE,
    DELETE,
    INSERT
};

// 事务操作记录
struct TransactionOperation {
    uint64_t transaction_id;
    OperationType type;
    std::string key;
    std::string old_value;
    std::string new_value;
    uint64_t timestamp;
    uint64_t version;
    
    TransactionOperation() : transaction_id(0), type(OperationType::READ), 
                           timestamp(0), version(0) {}
};

// 锁信息
struct LockInfo {
    uint64_t transaction_id;
    LockType lock_type;
    std::string resource_id;
    std::chrono::system_clock::time_point acquired_time;
    
    LockInfo() : transaction_id(0), lock_type(LockType::SHARED) {}
    
    LockInfo(uint64_t tid, LockType type, const std::string& resource)
        : transaction_id(tid), lock_type(type), resource_id(resource),
          acquired_time(std::chrono::system_clock::now()) {}
};

// 事务上下文
class TransactionContext {
public:
    TransactionContext(uint64_t id, IsolationLevel level);
    ~TransactionContext();
    
    // 基本属性
    uint64_t get_transaction_id() const { return transaction_id_; }
    TransactionState get_state() const { return state_; }
    IsolationLevel get_isolation_level() const { return isolation_level_; }
    
    // 状态管理
    void set_state(TransactionState state);
    bool is_active() const { return state_ == TransactionState::ACTIVE; }
    bool is_committed() const { return state_ == TransactionState::COMMITTED; }
    bool is_aborted() const { return state_ == TransactionState::ABORTED; }
    
    // 操作记录
    void add_operation(const TransactionOperation& op);
    const std::vector<TransactionOperation>& get_operations() const { return operations_; }
    
    // 读集和写集管理
    void add_read_set(const std::string& key, uint64_t version);
    void add_write_set(const std::string& key, const std::string& value);
    bool is_in_read_set(const std::string& key) const;
    bool is_in_write_set(const std::string& key) const;
    
    // 锁管理
    void add_lock(const LockInfo& lock);
    void remove_lock(const std::string& resource_id);
    const std::vector<LockInfo>& get_locks() const { return locks_; }
    
    // 时间戳
    uint64_t get_start_timestamp() const { return start_timestamp_; }
    uint64_t get_commit_timestamp() const { return commit_timestamp_; }
    void set_commit_timestamp(uint64_t timestamp) { commit_timestamp_ = timestamp; }
    
    // 快照
    void set_snapshot_version(uint64_t version) { snapshot_version_ = version; }
    uint64_t get_snapshot_version() const { return snapshot_version_; }

private:
    uint64_t transaction_id_;
    TransactionState state_;
    IsolationLevel isolation_level_;
    
    uint64_t start_timestamp_;
    uint64_t commit_timestamp_;
    uint64_t snapshot_version_;
    
    std::vector<TransactionOperation> operations_;
    std::unordered_map<std::string, uint64_t> read_set_;  // key -> version
    std::unordered_map<std::string, std::string> write_set_; // key -> value
    std::vector<LockInfo> locks_;
    
    mutable std::mutex context_mutex_;
};

// 事务管理器
class TransactionManager {
public:
    TransactionManager();
    ~TransactionManager();
    
    // 事务生命周期
    std::shared_ptr<TransactionContext> begin_transaction(
        IsolationLevel level = IsolationLevel::READ_COMMITTED);
    bool commit_transaction(uint64_t transaction_id);
    bool abort_transaction(uint64_t transaction_id);
    
    // 事务查询
    std::shared_ptr<TransactionContext> get_transaction(uint64_t transaction_id);
    std::vector<uint64_t> get_active_transactions() const;
    bool is_transaction_active(uint64_t transaction_id) const;
    
    // 锁管理
    bool acquire_lock(uint64_t transaction_id, const std::string& resource_id, 
                     LockType lock_type);
    bool release_lock(uint64_t transaction_id, const std::string& resource_id);
    void release_all_locks(uint64_t transaction_id);
    
    // 死锁检测
    bool detect_deadlock();
    std::vector<uint64_t> find_deadlock_cycle();
    bool resolve_deadlock(const std::vector<uint64_t>& cycle);
    
    // 冲突检测
    bool has_write_write_conflict(uint64_t transaction_id, const std::string& key);
    bool has_read_write_conflict(uint64_t transaction_id, const std::string& key);
    
    // 统计信息
    size_t get_active_transaction_count() const;
    size_t get_total_lock_count() const;
    double get_average_transaction_duration() const;
    
    // 清理
    void cleanup_completed_transactions();
    void set_transaction_timeout(std::chrono::milliseconds timeout);
    
    // 辅助方法（需要与数据库集成）
    bool validate_transaction(std::shared_ptr<TransactionContext> context);
    bool apply_transaction_writes(std::shared_ptr<TransactionContext> context);
    void rollback_transaction_operations(std::shared_ptr<TransactionContext> context);

private:
    std::atomic<uint64_t> next_transaction_id_;
    std::atomic<uint64_t> global_timestamp_;
    
    mutable std::mutex transactions_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<TransactionContext>> active_transactions_;
    std::unordered_map<uint64_t, std::shared_ptr<TransactionContext>> completed_transactions_;
    
    // 锁表：resource_id -> 持有该资源锁的事务列表
    mutable std::mutex locks_mutex_;
    std::unordered_map<std::string, std::vector<LockInfo>> lock_table_;
    
    // 等待图：用于死锁检测
    mutable std::mutex wait_graph_mutex_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> wait_graph_;
    
    std::chrono::milliseconds transaction_timeout_;
    
    // 私有方法
    uint64_t generate_transaction_id();
    uint64_t generate_timestamp();
    bool is_lock_compatible(LockType existing_lock, LockType requested_lock);
    void update_wait_graph(uint64_t waiting_txn, uint64_t holding_txn);
    void remove_from_wait_graph(uint64_t transaction_id);
    bool dfs_cycle_detection(uint64_t node, std::unordered_set<uint64_t>& visited,
                            std::unordered_set<uint64_t>& rec_stack,
                            std::vector<uint64_t>& cycle);
};