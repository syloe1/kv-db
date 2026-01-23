#pragma once

#include "transaction.h"
#include "../mvcc/mvcc_manager.h"
#include "../distributed_transaction/distributed_transaction_coordinator.h"
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <functional>

// 锁管理器前向声明
class LockManager;
class OptimisticLockManager;
class PessimisticLockManager;

// 增强的事务类型
enum class TransactionType {
    LOCAL,          // 本地事务
    DISTRIBUTED,    // 分布式事务
    READ_ONLY,      // 只读事务
    WRITE_ONLY      // 只写事务
};

// 并发控制策略
enum class ConcurrencyControlStrategy {
    OPTIMISTIC,     // 乐观并发控制
    PESSIMISTIC,    // 悲观并发控制
    MVCC,           // 多版本并发控制
    HYBRID          // 混合策略
};

// 事务性能指标
struct TransactionMetrics {
    std::chrono::milliseconds start_time;
    std::chrono::milliseconds prepare_time;
    std::chrono::milliseconds commit_time;
    std::chrono::milliseconds total_duration;
    
    size_t read_operations;
    size_t write_operations;
    size_t lock_acquisitions;
    size_t lock_waits;
    size_t conflicts;
    size_t retries;
    
    TransactionMetrics() : read_operations(0), write_operations(0),
                          lock_acquisitions(0), lock_waits(0),
                          conflicts(0), retries(0) {}
};

// 增强的事务上下文
class EnhancedTransactionContext : public TransactionContext {
public:
    EnhancedTransactionContext(uint64_t id, IsolationLevel level, 
                              TransactionType type = TransactionType::LOCAL,
                              ConcurrencyControlStrategy strategy = ConcurrencyControlStrategy::MVCC);
    
    ~EnhancedTransactionContext();
    
    // 事务类型
    TransactionType get_transaction_type() const { return transaction_type_; }
    void set_transaction_type(TransactionType type) { transaction_type_ = type; }
    
    // 并发控制策略
    ConcurrencyControlStrategy get_concurrency_strategy() const { return concurrency_strategy_; }
    void set_concurrency_strategy(ConcurrencyControlStrategy strategy) { concurrency_strategy_ = strategy; }
    
    // 性能指标
    const TransactionMetrics& get_metrics() const { return metrics_; }
    void update_metrics(const std::string& operation, std::chrono::milliseconds duration);
    void increment_conflict_count() { metrics_.conflicts++; }
    void increment_retry_count() { metrics_.retries++; }
    
    // 依赖跟踪
    void add_dependency(uint64_t dependent_txn_id);
    void remove_dependency(uint64_t dependent_txn_id);
    std::vector<uint64_t> get_dependencies() const;
    bool has_dependency(uint64_t txn_id) const;
    
    // 分布式事务支持
    void set_distributed_context(std::shared_ptr<DistributedTransactionContext> dist_context);
    std::shared_ptr<DistributedTransactionContext> get_distributed_context() const;
    bool is_distributed() const;
    
    // 乐观锁版本管理
    void set_expected_version(const std::string& key, uint64_t version);
    uint64_t get_expected_version(const std::string& key) const;
    bool validate_versions() const;
    
    // 悲观锁等待管理
    void add_waiting_for(uint64_t txn_id);
    void remove_waiting_for(uint64_t txn_id);
    std::vector<uint64_t> get_waiting_for() const;
    
    // 事务优先级
    void set_priority(int priority) { priority_ = priority; }
    int get_priority() const { return priority_; }

private:
    TransactionType transaction_type_;
    ConcurrencyControlStrategy concurrency_strategy_;
    TransactionMetrics metrics_;
    
    std::vector<uint64_t> dependencies_;
    std::shared_ptr<DistributedTransactionContext> distributed_context_;
    
    // 乐观锁版本期望
    std::unordered_map<std::string, uint64_t> expected_versions_;
    
    // 悲观锁等待关系
    std::vector<uint64_t> waiting_for_;
    
    int priority_;
    mutable std::mutex enhanced_mutex_;
};

// 增强的事务管理器
class EnhancedTransactionManager : public TransactionManager {
public:
    EnhancedTransactionManager(
        std::shared_ptr<MVCCManager> mvcc_manager = nullptr,
        std::shared_ptr<DistributedTransactionCoordinator> dist_coordinator = nullptr
    );
    
    ~EnhancedTransactionManager();
    
    // 增强的事务生命周期
    std::shared_ptr<EnhancedTransactionContext> begin_enhanced_transaction(
        IsolationLevel level = IsolationLevel::READ_COMMITTED,
        TransactionType type = TransactionType::LOCAL,
        ConcurrencyControlStrategy strategy = ConcurrencyControlStrategy::MVCC,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)
    );
    
    bool commit_enhanced_transaction(uint64_t transaction_id);
    bool abort_enhanced_transaction(uint64_t transaction_id);
    
    // 分布式事务支持
    std::shared_ptr<EnhancedTransactionContext> begin_distributed_transaction(
        const std::vector<DistributedOperation>& operations,
        IsolationLevel level = IsolationLevel::READ_COMMITTED,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)
    );
    
    // 数据操作接口
    bool read(uint64_t transaction_id, const std::string& key, std::string& value);
    bool write(uint64_t transaction_id, const std::string& key, const std::string& value);
    bool remove(uint64_t transaction_id, const std::string& key);
    
    // 乐观并发控制
    bool optimistic_read(uint64_t transaction_id, const std::string& key, 
                        std::string& value, uint64_t& version);
    bool optimistic_write(uint64_t transaction_id, const std::string& key, 
                         const std::string& value, uint64_t expected_version);
    bool validate_optimistic_transaction(uint64_t transaction_id);
    
    // 悲观并发控制
    bool pessimistic_read(uint64_t transaction_id, const std::string& key, std::string& value);
    bool pessimistic_write(uint64_t transaction_id, const std::string& key, const std::string& value);
    bool acquire_read_lock(uint64_t transaction_id, const std::string& key);
    bool acquire_write_lock(uint64_t transaction_id, const std::string& key);
    
    // 死锁检测和解决
    bool detect_deadlock_enhanced();
    std::vector<uint64_t> find_deadlock_cycle_enhanced();
    bool resolve_deadlock_enhanced(const std::vector<uint64_t>& cycle);
    
    // 事务恢复
    bool recover_transaction(uint64_t transaction_id);
    void recover_all_transactions();
    
    // 性能监控
    struct TransactionSystemStats {
        size_t total_transactions;
        size_t active_transactions;
        size_t committed_transactions;
        size_t aborted_transactions;
        size_t distributed_transactions;
        
        size_t optimistic_transactions;
        size_t pessimistic_transactions;
        size_t mvcc_transactions;
        
        double average_transaction_duration_ms;
        double commit_success_rate;
        double conflict_rate;
        
        size_t total_locks;
        size_t deadlocks_detected;
        size_t deadlocks_resolved;
        
        TransactionSystemStats() : total_transactions(0), active_transactions(0),
                                  committed_transactions(0), aborted_transactions(0),
                                  distributed_transactions(0), optimistic_transactions(0),
                                  pessimistic_transactions(0), mvcc_transactions(0),
                                  average_transaction_duration_ms(0.0), commit_success_rate(0.0),
                                  conflict_rate(0.0), total_locks(0), deadlocks_detected(0),
                                  deadlocks_resolved(0) {}
    };
    
    TransactionSystemStats get_system_statistics() const;
    void print_system_statistics() const;
    
    // 配置管理
    void set_default_isolation_level(IsolationLevel level);
    void set_default_concurrency_strategy(ConcurrencyControlStrategy strategy);
    void set_deadlock_detection_interval(std::chrono::milliseconds interval);
    void enable_performance_monitoring(bool enable);
    
    // 集成接口
    void set_mvcc_manager(std::shared_ptr<MVCCManager> mvcc_manager);
    void set_distributed_coordinator(std::shared_ptr<DistributedTransactionCoordinator> coordinator);
    void set_storage_engine(std::shared_ptr<void> storage_engine); // 通用存储引擎接口

private:
    std::shared_ptr<MVCCManager> mvcc_manager_;
    std::shared_ptr<DistributedTransactionCoordinator> distributed_coordinator_;
    std::shared_ptr<OptimisticLockManager> optimistic_lock_manager_;
    std::shared_ptr<PessimisticLockManager> pessimistic_lock_manager_;
    
    // 增强的事务存储
    mutable std::shared_mutex enhanced_transactions_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<EnhancedTransactionContext>> enhanced_active_transactions_;
    std::unordered_map<uint64_t, std::shared_ptr<EnhancedTransactionContext>> enhanced_completed_transactions_;
    
    // 配置
    IsolationLevel default_isolation_level_;
    ConcurrencyControlStrategy default_concurrency_strategy_;
    std::chrono::milliseconds deadlock_detection_interval_;
    bool performance_monitoring_enabled_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    TransactionSystemStats system_stats_;
    
    // 后台线程
    std::atomic<bool> background_running_;
    std::thread deadlock_detector_thread_;
    std::thread performance_monitor_thread_;
    
    // 私有方法
    void start_background_threads();
    void stop_background_threads();
    void deadlock_detection_loop();
    void performance_monitoring_loop();
    
    // 事务执行策略
    bool execute_with_mvcc(std::shared_ptr<EnhancedTransactionContext> context,
                          const std::function<bool()>& operation);
    bool execute_with_optimistic_cc(std::shared_ptr<EnhancedTransactionContext> context,
                                   const std::function<bool()>& operation);
    bool execute_with_pessimistic_cc(std::shared_ptr<EnhancedTransactionContext> context,
                                    const std::function<bool()>& operation);
    
    // 冲突检测和解决
    bool detect_write_write_conflict_enhanced(uint64_t transaction_id, const std::string& key);
    bool detect_read_write_conflict_enhanced(uint64_t transaction_id, const std::string& key);
    void resolve_conflict(uint64_t transaction_id, const std::string& key);
    
    // 统计更新
    void update_system_statistics();
    void record_transaction_start(std::shared_ptr<EnhancedTransactionContext> context);
    void record_transaction_completion(std::shared_ptr<EnhancedTransactionContext> context, bool committed);
    
    // 工具方法
    std::shared_ptr<EnhancedTransactionContext> get_enhanced_transaction(uint64_t transaction_id);
    bool validate_transaction_compatibility(std::shared_ptr<EnhancedTransactionContext> context);
    void cleanup_enhanced_completed_transactions();
};