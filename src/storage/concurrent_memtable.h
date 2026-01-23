#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_vector.h>
#include "storage/versioned_value.h"

// 无锁MemTable实现
class ConcurrentMemTable {
public:
    ConcurrentMemTable();
    ~ConcurrentMemTable();
    
    // 并发安全的操作
    void put(const std::string& key, const std::string& value, uint64_t seq);
    bool get(const std::string& key, uint64_t snapshot_seq, std::string& value) const;
    void del(const std::string& key, uint64_t seq);
    
    // 批量操作
    void batch_put(const std::vector<std::tuple<std::string, std::string, uint64_t>>& operations);
    
    size_t size() const;
    void clear();
    
    // 获取所有数据用于flush
    std::map<std::string, std::vector<VersionedValue>> get_all_versions() const;
    
    // 并发统计
    struct ConcurrentStats {
        std::atomic<uint64_t> total_puts{0};
        std::atomic<uint64_t> total_gets{0};
        std::atomic<uint64_t> total_deletes{0};
        std::atomic<uint64_t> lock_contentions{0};
        std::atomic<uint64_t> batch_operations{0};
    };
    
    const ConcurrentStats& get_stats() const { return stats_; }
    void print_stats() const;
    
private:
    // 使用Intel TBB的并发数据结构
    using ConcurrentMap = tbb::concurrent_hash_map<std::string, tbb::concurrent_vector<VersionedValue>>;
    ConcurrentMap table_;
    
    mutable std::atomic<size_t> size_bytes_{0};
    mutable ConcurrentStats stats_;
    
    static const std::string TOMBSTONE;
};

// 读写分离的MemTable包装器
class ReadWriteSeparatedMemTable {
public:
    ReadWriteSeparatedMemTable();
    
    // 读操作 - 无锁或细粒度锁
    bool get(const std::string& key, uint64_t snapshot_seq, std::string& value) const;
    
    // 写操作 - 批量处理
    void put(const std::string& key, const std::string& value, uint64_t seq);
    void del(const std::string& key, uint64_t seq);
    void batch_write(const std::vector<std::tuple<std::string, std::string, uint64_t>>& operations);
    
    size_t size() const;
    void clear();
    std::map<std::string, std::vector<VersionedValue>> get_all_versions() const;
    
    // 并发控制
    void begin_read() const;
    void end_read() const;
    void begin_write();
    void end_write();
    
private:
    std::unique_ptr<ConcurrentMemTable> active_table_;
    std::unique_ptr<ConcurrentMemTable> immutable_table_;
    
    mutable std::shared_mutex rw_mutex_;
    mutable std::atomic<int> active_readers_{0};
    
    // 写入缓冲区
    struct WriteBuffer {
        std::vector<std::tuple<std::string, std::string, uint64_t>> pending_writes;
        std::mutex buffer_mutex;
        std::condition_variable buffer_cv;
        std::atomic<bool> flush_requested{false};
    };
    
    WriteBuffer write_buffer_;
    std::thread write_worker_;
    std::atomic<bool> stop_worker_{false};
    
    void write_worker_thread();
    void flush_write_buffer();
};