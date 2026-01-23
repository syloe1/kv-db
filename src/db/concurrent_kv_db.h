#pragma once
#include "db/kv_db.h"
#include "storage/concurrent_memtable.h"
#include "concurrent/coroutine_processor.h"
#include <memory>
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <vector>
#include <future>

// 并发优化的KV数据库
class ConcurrentKVDB {
public:
    explicit ConcurrentKVDB(const std::string& wal_file, size_t num_threads = std::thread::hardware_concurrency());
    ~ConcurrentKVDB();
    
    // 基础操作 - 并发优化版本
    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    bool del(const std::string& key);
    
    // 异步操作
    std::future<bool> async_put(const std::string& key, const std::string& value);
    std::future<std::optional<std::string>> async_get(const std::string& key);
    std::future<bool> async_delete(const std::string& key);
    
    // 批量操作 - 减少锁竞争
    bool batch_put(const std::vector<std::pair<std::string, std::string>>& operations);
    std::vector<std::optional<std::string>> batch_get(const std::vector<std::string>& keys);
    bool batch_delete(const std::vector<std::string>& keys);
    
    // 异步批量操作
    std::future<std::vector<bool>> async_batch_put(
        const std::vector<std::pair<std::string, std::string>>& operations);
    std::future<std::vector<std::optional<std::string>>> async_batch_get(
        const std::vector<std::string>& keys);
    
    // 读写分离操作
    bool read_only_get(const std::string& key, std::string& value) const;
    std::vector<std::optional<std::string>> read_only_batch_get(
        const std::vector<std::string>& keys) const;
    
    // 并发控制
    void enable_lock_free_reads(bool enable = true);
    void set_batch_size(size_t batch_size);
    void set_write_buffer_size(size_t buffer_size);
    
    // 性能监控
    struct ConcurrentPerformanceStats {
        std::atomic<uint64_t> concurrent_reads{0};
        std::atomic<uint64_t> concurrent_writes{0};
        std::atomic<uint64_t> lock_free_reads{0};
        std::atomic<uint64_t> batch_operations{0};
        std::atomic<uint64_t> lock_contentions{0};
        std::atomic<uint64_t> async_operations{0};
        
        std::atomic<double> avg_read_latency{0.0};
        std::atomic<double> avg_write_latency{0.0};
        std::atomic<double> throughput_qps{0.0};
    };
    
    const ConcurrentPerformanceStats& get_performance_stats() const { return perf_stats_; }
    void print_performance_stats() const;
    void reset_stats();
    
    // 传统接口兼容
    size_t get_memtable_size() const;
    double get_cache_hit_rate() const;
    void flush();
    
private:
    // 核心组件
    std::unique_ptr<ReadWriteSeparatedMemTable> concurrent_memtable_;
    std::unique_ptr<CoroutineScheduler> scheduler_;
    std::unique_ptr<ConcurrentDBOperations> concurrent_ops_;
    
    // 传统组件（用于兼容）
    std::unique_ptr<KVDB> legacy_db_;
    
    // 并发控制
    mutable std::shared_mutex global_rw_mutex_;
    std::atomic<uint64_t> next_seq_{0};
    
    // 配置参数
    std::atomic<bool> lock_free_reads_enabled_{true};
    std::atomic<size_t> batch_size_{100};
    std::atomic<size_t> write_buffer_size_{1000};
    
    // 性能统计
    mutable ConcurrentPerformanceStats perf_stats_;
    
    // 性能监控线程
    std::thread perf_monitor_thread_;
    std::atomic<bool> stop_perf_monitor_{false};
    
    // 内部方法
    uint64_t get_next_seq();
    void performance_monitor_worker();
    void update_latency_stats(double read_latency, double write_latency);
    
    // 读写分离实现
    template<typename Func>
    auto execute_read_operation(Func&& func) const -> decltype(func()) {
        if (lock_free_reads_enabled_.load()) {
            perf_stats_.lock_free_reads.fetch_add(1);
            return func();
        } else {
            std::shared_lock<std::shared_mutex> lock(global_rw_mutex_);
            perf_stats_.concurrent_reads.fetch_add(1);
            return func();
        }
    }
    
    template<typename Func>
    auto execute_write_operation(Func&& func) -> decltype(func()) {
        std::unique_lock<std::shared_mutex> lock(global_rw_mutex_);
        perf_stats_.concurrent_writes.fetch_add(1);
        return func();
    }
};