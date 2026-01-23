#include "concurrent_kv_db.h"
#include <iostream>
#include <chrono>
#include <algorithm>

ConcurrentKVDB::ConcurrentKVDB(const std::string& wal_file, size_t num_threads)
    : concurrent_memtable_(std::make_unique<ReadWriteSeparatedMemTable>()),
      scheduler_(std::make_unique<CoroutineScheduler>(num_threads)),
      concurrent_ops_(std::make_unique<ConcurrentDBOperations>(*scheduler_)),
      legacy_db_(std::make_unique<KVDB>(wal_file)),
      perf_monitor_thread_(&ConcurrentKVDB::performance_monitor_worker, this) {
    
    std::cout << "[ConcurrentKVDB] 初始化并发数据库，线程数: " << num_threads << "\n";
    std::cout << "[ConcurrentKVDB] 启用读写分离和无锁优化\n";
}

ConcurrentKVDB::~ConcurrentKVDB() {
    stop_perf_monitor_.store(true);
    if (perf_monitor_thread_.joinable()) {
        perf_monitor_thread_.join();
    }
}

uint64_t ConcurrentKVDB::get_next_seq() {
    return next_seq_.fetch_add(1);
}

// 基础操作实现
bool ConcurrentKVDB::put(const std::string& key, const std::string& value) {
    auto start = std::chrono::high_resolution_clock::now();
    
    bool result = execute_write_operation([&]() {
        uint64_t seq = get_next_seq();
        concurrent_memtable_->put(key, value, seq);
        return true;
    });
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start).count();
    update_latency_stats(0.0, duration);
    
    return result;
}

bool ConcurrentKVDB::get(const std::string& key, std::string& value) {
    auto start = std::chrono::high_resolution_clock::now();
    
    bool result = execute_read_operation([&]() {
        uint64_t snapshot_seq = next_seq_.load();
        return concurrent_memtable_->get(key, snapshot_seq, value);
    });
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start).count();
    update_latency_stats(duration, 0.0);
    
    return result;
}

bool ConcurrentKVDB::del(const std::string& key) {
    return execute_write_operation([&]() {
        uint64_t seq = get_next_seq();
        concurrent_memtable_->del(key, seq);
        return true;
    });
}

// 异步操作实现
std::future<bool> ConcurrentKVDB::async_put(const std::string& key, const std::string& value) {
    perf_stats_.async_operations.fetch_add(1);
    
    return scheduler_->submit([this, key, value]() {
        return put(key, value);
    });
}

std::future<std::optional<std::string>> ConcurrentKVDB::async_get(const std::string& key) {
    perf_stats_.async_operations.fetch_add(1);
    
    return scheduler_->submit([this, key]() -> std::optional<std::string> {
        std::string value;
        if (get(key, value)) {
            return value;
        }
        return std::nullopt;
    });
}

std::future<bool> ConcurrentKVDB::async_delete(const std::string& key) {
    perf_stats_.async_operations.fetch_add(1);
    
    return scheduler_->submit([this, key]() {
        return del(key);
    });
}

// 批量操作实现
bool ConcurrentKVDB::batch_put(const std::vector<std::pair<std::string, std::string>>& operations) {
    perf_stats_.batch_operations.fetch_add(1);
    
    return execute_write_operation([&]() {
        std::vector<std::tuple<std::string, std::string, uint64_t>> batch_ops;
        batch_ops.reserve(operations.size());
        
        for (const auto& [key, value] : operations) {
            batch_ops.emplace_back(key, value, get_next_seq());
        }
        
        concurrent_memtable_->batch_write(batch_ops);
        return true;
    });
}

std::vector<std::optional<std::string>> ConcurrentKVDB::batch_get(const std::vector<std::string>& keys) {
    perf_stats_.batch_operations.fetch_add(1);
    
    return execute_read_operation([&]() {
        std::vector<std::optional<std::string>> results;
        results.reserve(keys.size());
        
        uint64_t snapshot_seq = next_seq_.load();
        
        for (const auto& key : keys) {
            std::string value;
            if (concurrent_memtable_->get(key, snapshot_seq, value)) {
                results.emplace_back(value);
            } else {
                results.emplace_back(std::nullopt);
            }
        }
        
        return results;
    });
}

bool ConcurrentKVDB::batch_delete(const std::vector<std::string>& keys) {
    perf_stats_.batch_operations.fetch_add(1);
    
    return execute_write_operation([&]() {
        std::vector<std::tuple<std::string, std::string, uint64_t>> batch_ops;
        batch_ops.reserve(keys.size());
        
        for (const auto& key : keys) {
            batch_ops.emplace_back(key, "__TOMBSTONE__", get_next_seq());
        }
        
        concurrent_memtable_->batch_write(batch_ops);
        return true;
    });
}

// 异步批量操作
std::future<std::vector<bool>> ConcurrentKVDB::async_batch_put(
    const std::vector<std::pair<std::string, std::string>>& operations) {
    
    perf_stats_.async_operations.fetch_add(1);
    
    return scheduler_->submit([this, operations]() {
        std::vector<bool> results;
        results.reserve(operations.size());
        
        bool success = batch_put(operations);
        for (size_t i = 0; i < operations.size(); ++i) {
            results.push_back(success);
        }
        
        return results;
    });
}

std::future<std::vector<std::optional<std::string>>> ConcurrentKVDB::async_batch_get(
    const std::vector<std::string>& keys) {
    
    perf_stats_.async_operations.fetch_add(1);
    
    return scheduler_->submit([this, keys]() {
        return batch_get(keys);
    });
}

// 只读操作
bool ConcurrentKVDB::read_only_get(const std::string& key, std::string& value) const {
    perf_stats_.lock_free_reads.fetch_add(1);
    
    // 完全无锁的读操作
    uint64_t snapshot_seq = next_seq_.load();
    return concurrent_memtable_->get(key, snapshot_seq, value);
}

std::vector<std::optional<std::string>> ConcurrentKVDB::read_only_batch_get(
    const std::vector<std::string>& keys) const {
    
    perf_stats_.lock_free_reads.fetch_add(1);
    
    std::vector<std::optional<std::string>> results;
    results.reserve(keys.size());
    
    uint64_t snapshot_seq = next_seq_.load();
    
    for (const auto& key : keys) {
        std::string value;
        if (concurrent_memtable_->get(key, snapshot_seq, value)) {
            results.emplace_back(value);
        } else {
            results.emplace_back(std::nullopt);
        }
    }
    
    return results;
}

// 配置方法
void ConcurrentKVDB::enable_lock_free_reads(bool enable) {
    lock_free_reads_enabled_.store(enable);
    std::cout << "[ConcurrentKVDB] " << (enable ? "启用" : "禁用") << "无锁读取\n";
}

void ConcurrentKVDB::set_batch_size(size_t batch_size) {
    batch_size_.store(batch_size);
    std::cout << "[ConcurrentKVDB] 设置批量操作大小: " << batch_size << "\n";
}

void ConcurrentKVDB::set_write_buffer_size(size_t buffer_size) {
    write_buffer_size_.store(buffer_size);
    std::cout << "[ConcurrentKVDB] 设置写缓冲区大小: " << buffer_size << "\n";
}

// 性能监控
void ConcurrentKVDB::performance_monitor_worker() {
    auto last_time = std::chrono::steady_clock::now();
    uint64_t last_reads = 0;
    uint64_t last_writes = 0;
    
    while (!stop_perf_monitor_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        auto current_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration<double>(current_time - last_time).count();
        
        uint64_t current_reads = perf_stats_.concurrent_reads.load() + perf_stats_.lock_free_reads.load();
        uint64_t current_writes = perf_stats_.concurrent_writes.load();
        
        double read_qps = (current_reads - last_reads) / duration;
        double write_qps = (current_writes - last_writes) / duration;
        double total_qps = read_qps + write_qps;
        
        perf_stats_.throughput_qps.store(total_qps);
        
        last_time = current_time;
        last_reads = current_reads;
        last_writes = current_writes;
    }
}

void ConcurrentKVDB::update_latency_stats(double read_latency, double write_latency) {
    if (read_latency > 0) {
        // 简单的移动平均
        double current_avg = perf_stats_.avg_read_latency.load();
        perf_stats_.avg_read_latency.store((current_avg * 0.9) + (read_latency * 0.1));
    }
    
    if (write_latency > 0) {
        double current_avg = perf_stats_.avg_write_latency.load();
        perf_stats_.avg_write_latency.store((current_avg * 0.9) + (write_latency * 0.1));
    }
}

void ConcurrentKVDB::print_performance_stats() const {
    std::cout << "\n=== 并发数据库性能统计 ===\n";
    std::cout << "并发读取次数: " << perf_stats_.concurrent_reads.load() << "\n";
    std::cout << "并发写入次数: " << perf_stats_.concurrent_writes.load() << "\n";
    std::cout << "无锁读取次数: " << perf_stats_.lock_free_reads.load() << "\n";
    std::cout << "批量操作次数: " << perf_stats_.batch_operations.load() << "\n";
    std::cout << "锁竞争次数: " << perf_stats_.lock_contentions.load() << "\n";
    std::cout << "异步操作次数: " << perf_stats_.async_operations.load() << "\n";
    std::cout << "平均读取延迟: " << perf_stats_.avg_read_latency.load() << " ms\n";
    std::cout << "平均写入延迟: " << perf_stats_.avg_write_latency.load() << " ms\n";
    std::cout << "吞吐量: " << perf_stats_.throughput_qps.load() << " QPS\n";
    
    // 计算并发提升比例
    uint64_t total_reads = perf_stats_.concurrent_reads.load() + perf_stats_.lock_free_reads.load();
    uint64_t lock_free_ratio = total_reads > 0 ? 
        (perf_stats_.lock_free_reads.load() * 100 / total_reads) : 0;
    
    std::cout << "无锁读取比例: " << lock_free_ratio << "%\n";
    std::cout << "========================\n\n";
}

void ConcurrentKVDB::reset_stats() {
    perf_stats_.concurrent_reads.store(0);
    perf_stats_.concurrent_writes.store(0);
    perf_stats_.lock_free_reads.store(0);
    perf_stats_.batch_operations.store(0);
    perf_stats_.lock_contentions.store(0);
    perf_stats_.async_operations.store(0);
    perf_stats_.avg_read_latency.store(0.0);
    perf_stats_.avg_write_latency.store(0.0);
    perf_stats_.throughput_qps.store(0.0);
}

// 兼容接口
size_t ConcurrentKVDB::get_memtable_size() const {
    return concurrent_memtable_->size();
}

double ConcurrentKVDB::get_cache_hit_rate() const {
    return legacy_db_->get_cache_hit_rate();
}

void ConcurrentKVDB::flush() {
    execute_write_operation([&]() {
        legacy_db_->flush();
    });
}