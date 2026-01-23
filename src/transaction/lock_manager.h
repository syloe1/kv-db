#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <queue>

// 锁模式
enum class LockMode {
    NONE,           // 无锁
    SHARED,         // 共享锁（读锁）
    EXCLUSIVE,      // 排他锁（写锁）
    INTENTION_SHARED,    // 意向共享锁
    INTENTION_EXCLUSIVE, // 意向排他锁
    SHARED_INTENTION_EXCLUSIVE // 共享意向排他锁
};

// 锁请求
struct LockRequest {
    uint64_t transaction_id;
    std::string resource_id;
    LockMode lock_mode;
    std::chrono::system_clock::time_point request_time;
    std::chrono::milliseconds timeout;
    bool granted;
    
    LockRequest(uint64_t txn_id, const std::string& resource, LockMode mode,
                std::chrono::milliseconds timeout_ms = std::chrono::milliseconds(30000))
        : transaction_id(txn_id), resource_id(resource), lock_mode(mode),
          request_time(std::chrono::system_clock::now()), timeout(timeout_ms), granted(false) {}
    
    bool is_timeout() const {
        auto now = std::chrono::system_clock::now();
        return (now - request_time) > timeout;
    }
};

// 锁表项
struct LockTableEntry {
    std::string resource_id;
    std::vector<std::shared_ptr<LockRequest>> granted_locks;    // 已授予的锁
    std::queue<std::shared_ptr<LockRequest>> waiting_queue;     // 等待队列
    mutable std::mutex entry_mutex;
    std::condition_variable entry_cv;
    
    LockTableEntry(const std::string& resource) : resource_id(resource) {}
    
    bool has_conflicting_lock(LockMode requested_mode, uint64_t requesting_txn) const;
    bool can_grant_lock(LockMode requested_mode, uint64_t requesting_txn) const;
    void grant_compatible_locks();
    size_t get_waiting_count() const { return waiting_queue.size(); }
    size_t get_granted_count() const { return granted_locks.size(); }
};

// 基础锁管理器
class LockManager {
public:
    LockManager();
    virtual ~LockManager();
    
    // 锁操作
    virtual bool acquire_lock(uint64_t transaction_id, const std::string& resource_id, 
                             LockMode lock_mode, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)) = 0;
    virtual bool release_lock(uint64_t transaction_id, const std::string& resource_id) = 0;
    virtual void release_all_locks(uint64_t transaction_id) = 0;
    
    // 查询接口
    virtual bool has_lock(uint64_t transaction_id, const std::string& resource_id) const = 0;
    virtual LockMode get_lock_mode(uint64_t transaction_id, const std::string& resource_id) const = 0;
    virtual std::vector<std::string> get_locked_resources(uint64_t transaction_id) const = 0;
    
    // 死锁检测
    virtual bool detect_deadlock() = 0;
    virtual std::vector<uint64_t> find_deadlock_cycle() = 0;
    
    // 统计信息
    struct LockManagerStats {
        size_t total_locks;
        size_t waiting_requests;
        size_t granted_requests;
        size_t timeout_requests;
        size_t deadlocks_detected;
        double average_wait_time_ms;
        
        LockManagerStats() : total_locks(0), waiting_requests(0), granted_requests(0),
                            timeout_requests(0), deadlocks_detected(0), average_wait_time_ms(0.0) {}
    };
    
    virtual LockManagerStats get_statistics() const = 0;
    virtual void print_statistics() const = 0;

public:
    // 锁兼容性检查
    static bool is_compatible(LockMode existing_mode, LockMode requested_mode);
    static bool is_upgrade_compatible(LockMode current_mode, LockMode requested_mode);

protected:
    
    // 工具方法
    std::shared_ptr<LockTableEntry> get_or_create_lock_entry(const std::string& resource_id);
    void cleanup_empty_entries();
    
    // 锁表
    mutable std::shared_mutex lock_table_mutex_;
    std::unordered_map<std::string, std::shared_ptr<LockTableEntry>> lock_table_;
    
    // 事务锁映射
    mutable std::mutex transaction_locks_mutex_;
    std::unordered_map<uint64_t, std::unordered_set<std::string>> transaction_locks_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    LockManagerStats stats_;
};

// 悲观锁管理器
class PessimisticLockManager : public LockManager {
public:
    PessimisticLockManager();
    ~PessimisticLockManager();
    
    // 锁操作实现
    bool acquire_lock(uint64_t transaction_id, const std::string& resource_id, 
                     LockMode lock_mode, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)) override;
    bool release_lock(uint64_t transaction_id, const std::string& resource_id) override;
    void release_all_locks(uint64_t transaction_id) override;
    
    // 查询接口实现
    bool has_lock(uint64_t transaction_id, const std::string& resource_id) const override;
    LockMode get_lock_mode(uint64_t transaction_id, const std::string& resource_id) const override;
    std::vector<std::string> get_locked_resources(uint64_t transaction_id) const override;
    
    // 死锁检测实现
    bool detect_deadlock() override;
    std::vector<uint64_t> find_deadlock_cycle() override;
    
    // 统计信息
    LockManagerStats get_statistics() const override;
    void print_statistics() const override;
    
    // 悲观锁特有功能
    bool upgrade_lock(uint64_t transaction_id, const std::string& resource_id, LockMode new_mode);
    bool downgrade_lock(uint64_t transaction_id, const std::string& resource_id, LockMode new_mode);
    
    // 锁等待管理
    void set_deadlock_detection_enabled(bool enabled);
    void set_lock_timeout(std::chrono::milliseconds timeout);

private:
    // 等待图（用于死锁检测）
    mutable std::mutex wait_graph_mutex_;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> wait_graph_;
    
    // 配置
    bool deadlock_detection_enabled_;
    std::chrono::milliseconds default_lock_timeout_;
    
    // 私有方法
    bool try_acquire_lock_immediate(std::shared_ptr<LockRequest> request, 
                                   std::shared_ptr<LockTableEntry> entry);
    bool wait_for_lock(std::shared_ptr<LockRequest> request, 
                      std::shared_ptr<LockTableEntry> entry);
    void update_wait_graph(uint64_t waiting_txn, const std::vector<uint64_t>& blocking_txns);
    void remove_from_wait_graph(uint64_t transaction_id);
    bool dfs_cycle_detection(uint64_t node, std::unordered_set<uint64_t>& visited,
                            std::unordered_set<uint64_t>& rec_stack, std::vector<uint64_t>& cycle);
    std::vector<uint64_t> get_blocking_transactions(const std::string& resource_id, 
                                                   LockMode requested_mode, uint64_t requesting_txn);
};

// 乐观锁管理器
class OptimisticLockManager : public LockManager {
public:
    OptimisticLockManager();
    ~OptimisticLockManager();
    
    // 基础锁操作（乐观锁中主要用于版本管理）
    bool acquire_lock(uint64_t transaction_id, const std::string& resource_id, 
                     LockMode lock_mode, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)) override;
    bool release_lock(uint64_t transaction_id, const std::string& resource_id) override;
    void release_all_locks(uint64_t transaction_id) override;
    
    // 查询接口
    bool has_lock(uint64_t transaction_id, const std::string& resource_id) const override;
    LockMode get_lock_mode(uint64_t transaction_id, const std::string& resource_id) const override;
    std::vector<std::string> get_locked_resources(uint64_t transaction_id) const override;
    
    // 死锁检测（乐观锁中不需要）
    bool detect_deadlock() override { return false; }
    std::vector<uint64_t> find_deadlock_cycle() override { return {}; }
    
    // 统计信息
    LockManagerStats get_statistics() const override;
    void print_statistics() const override;
    
    // 乐观锁特有功能
    bool read_with_version(uint64_t transaction_id, const std::string& key, 
                          std::string& value, uint64_t& version);
    bool write_with_version_check(uint64_t transaction_id, const std::string& key, 
                                 const std::string& value, uint64_t expected_version);
    bool validate_transaction(uint64_t transaction_id);
    
    // 版本管理
    uint64_t get_current_version(const std::string& key) const;
    void increment_version(const std::string& key);
    void set_version(const std::string& key, uint64_t version);
    
    // 读集和写集管理
    void add_to_read_set(uint64_t transaction_id, const std::string& key, uint64_t version);
    void add_to_write_set(uint64_t transaction_id, const std::string& key, const std::string& value);
    bool validate_read_set(uint64_t transaction_id);
    bool apply_write_set(uint64_t transaction_id);

private:
    // 版本信息
    mutable std::shared_mutex version_mutex_;
    std::unordered_map<std::string, uint64_t> key_versions_;
    
    // 事务读集和写集
    struct TransactionSets {
        std::unordered_map<std::string, uint64_t> read_set;     // key -> version
        std::unordered_map<std::string, std::string> write_set; // key -> value
        std::chrono::system_clock::time_point start_time;
        
        TransactionSets() : start_time(std::chrono::system_clock::now()) {}
    };
    
    mutable std::mutex transaction_sets_mutex_;
    std::unordered_map<uint64_t, TransactionSets> transaction_sets_;
    
    // 全局版本计数器
    std::atomic<uint64_t> global_version_counter_;
    
    // 私有方法
    uint64_t generate_version();
    bool has_version_conflict(const std::string& key, uint64_t expected_version);
    void cleanup_transaction_sets(uint64_t transaction_id);
};

// 混合锁管理器（结合悲观和乐观策略）
class HybridLockManager : public LockManager {
public:
    HybridLockManager(std::shared_ptr<PessimisticLockManager> pessimistic_manager,
                     std::shared_ptr<OptimisticLockManager> optimistic_manager);
    ~HybridLockManager();
    
    // 锁操作
    bool acquire_lock(uint64_t transaction_id, const std::string& resource_id, 
                     LockMode lock_mode, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)) override;
    bool release_lock(uint64_t transaction_id, const std::string& resource_id) override;
    void release_all_locks(uint64_t transaction_id) override;
    
    // 查询接口
    bool has_lock(uint64_t transaction_id, const std::string& resource_id) const override;
    LockMode get_lock_mode(uint64_t transaction_id, const std::string& resource_id) const override;
    std::vector<std::string> get_locked_resources(uint64_t transaction_id) const override;
    
    // 死锁检测
    bool detect_deadlock() override;
    std::vector<uint64_t> find_deadlock_cycle() override;
    
    // 统计信息
    LockManagerStats get_statistics() const override;
    void print_statistics() const override;
    
    // 混合策略配置
    void set_strategy_for_transaction(uint64_t transaction_id, bool use_optimistic);
    void set_default_strategy(bool use_optimistic);
    void set_conflict_threshold(double threshold); // 冲突率阈值，超过则切换到悲观锁
    
    // 自适应策略
    void enable_adaptive_strategy(bool enable);
    void update_conflict_statistics(uint64_t transaction_id, bool had_conflict);

private:
    std::shared_ptr<PessimisticLockManager> pessimistic_manager_;
    std::shared_ptr<OptimisticLockManager> optimistic_manager_;
    
    // 策略选择
    mutable std::mutex strategy_mutex_;
    std::unordered_map<uint64_t, bool> transaction_strategies_; // txn_id -> use_optimistic
    bool default_use_optimistic_;
    
    // 自适应策略
    bool adaptive_strategy_enabled_;
    double conflict_threshold_;
    
    // 冲突统计
    struct ConflictStats {
        size_t total_transactions;
        size_t conflicts;
        double conflict_rate;
        
        ConflictStats() : total_transactions(0), conflicts(0), conflict_rate(0.0) {}
        
        void update(bool had_conflict) {
            total_transactions++;
            if (had_conflict) conflicts++;
            conflict_rate = static_cast<double>(conflicts) / total_transactions;
        }
    };
    
    mutable std::mutex conflict_stats_mutex_;
    ConflictStats global_conflict_stats_;
    
    // 私有方法
    bool should_use_optimistic(uint64_t transaction_id);
    LockManager* get_manager_for_transaction(uint64_t transaction_id);
    void update_adaptive_strategy();
};