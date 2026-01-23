#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <thread>

// 版本化值
struct VersionedValue {
    std::string value;
    uint64_t version;           // 版本号
    uint64_t create_timestamp;  // 创建时间戳
    uint64_t delete_timestamp;  // 删除时间戳（0表示未删除）
    uint64_t transaction_id;    // 创建该版本的事务ID
    bool is_committed;          // 是否已提交
    
    VersionedValue() : version(0), create_timestamp(0), delete_timestamp(0),
                      transaction_id(0), is_committed(false) {}
    
    VersionedValue(const std::string& val, uint64_t ver, uint64_t create_ts, uint64_t txn_id)
        : value(val), version(ver), create_timestamp(create_ts), 
          delete_timestamp(0), transaction_id(txn_id), is_committed(false) {}
    
    bool is_visible_to(uint64_t read_timestamp) const {
        return is_committed && 
               create_timestamp <= read_timestamp && 
               (delete_timestamp == 0 || delete_timestamp > read_timestamp);
    }
    
    bool is_deleted_at(uint64_t read_timestamp) const {
        return delete_timestamp > 0 && delete_timestamp <= read_timestamp;
    }
};

// 版本链
class VersionChain {
public:
    VersionChain(const std::string& key);
    ~VersionChain();
    
    // 版本操作
    bool add_version(const VersionedValue& version);
    VersionedValue* get_version(uint64_t read_timestamp);
    VersionedValue* get_latest_version();
    std::vector<VersionedValue*> get_all_versions();
    
    // 删除操作
    bool mark_deleted(uint64_t version, uint64_t delete_timestamp);
    
    // 垃圾回收
    size_t cleanup_old_versions(uint64_t min_active_timestamp);
    
    // 统计信息
    size_t get_version_count() const;
    uint64_t get_oldest_version() const;
    uint64_t get_latest_version_number() const;
    
    const std::string& get_key() const { return key_; }

private:
    std::string key_;
    std::vector<std::unique_ptr<VersionedValue>> versions_;
    mutable std::shared_mutex versions_mutex_;
    
    // 辅助方法
    VersionedValue* find_version_for_timestamp(uint64_t timestamp);
    void sort_versions_by_timestamp();
};

// MVCC管理器
class MVCCManager {
public:
    MVCCManager();
    ~MVCCManager();
    
    // 读操作
    std::string read(const std::string& key, uint64_t read_timestamp);
    bool read(const std::string& key, uint64_t read_timestamp, std::string& value);
    
    // 写操作
    bool write(const std::string& key, const std::string& value, 
              uint64_t transaction_id, uint64_t write_timestamp);
    
    // 删除操作
    bool remove(const std::string& key, uint64_t transaction_id, uint64_t delete_timestamp);
    
    // 事务提交/中止
    void commit_transaction(uint64_t transaction_id, uint64_t commit_timestamp);
    void abort_transaction(uint64_t transaction_id);
    
    // 快照读
    std::unordered_map<std::string, std::string> create_snapshot(uint64_t snapshot_timestamp);
    bool read_from_snapshot(const std::string& key, uint64_t snapshot_timestamp, 
                           std::string& value);
    
    // 版本管理
    size_t get_total_versions() const;
    size_t get_version_count(const std::string& key) const;
    std::vector<std::string> get_all_keys() const;
    
    // 垃圾回收
    void start_garbage_collection();
    void stop_garbage_collection();
    size_t run_garbage_collection();
    void set_gc_interval(std::chrono::milliseconds interval);
    
    // 统计信息
    struct MVCCStats {
        size_t total_keys;
        size_t total_versions;
        size_t active_transactions;
        uint64_t oldest_active_timestamp;
        uint64_t latest_timestamp;
        size_t gc_runs;
        size_t versions_cleaned;
    };
    
    MVCCStats get_statistics() const;
    void print_statistics() const;
    
    // 配置
    void set_max_versions_per_key(size_t max_versions);
    void set_version_retention_time(std::chrono::milliseconds retention_time);

private:
    // 版本链存储
    mutable std::shared_mutex data_mutex_;
    std::unordered_map<std::string, std::unique_ptr<VersionChain>> version_chains_;
    
    // 活跃事务跟踪
    mutable std::mutex active_transactions_mutex_;
    std::unordered_map<uint64_t, uint64_t> active_transactions_; // txn_id -> start_timestamp
    
    // 时间戳管理
    std::atomic<uint64_t> global_timestamp_;
    
    // 垃圾回收
    std::atomic<bool> gc_running_;
    std::thread gc_thread_;
    std::chrono::milliseconds gc_interval_;
    mutable std::mutex gc_mutex_;
    
    // 配置参数
    size_t max_versions_per_key_;
    std::chrono::milliseconds version_retention_time_;
    
    // 统计信息
    mutable std::atomic<size_t> gc_runs_;
    mutable std::atomic<size_t> versions_cleaned_;
    
    // 私有方法
    VersionChain* get_or_create_version_chain(const std::string& key);
    uint64_t get_min_active_timestamp() const;
    void garbage_collection_worker();
    uint64_t generate_timestamp();
    
    // 事务管理辅助方法
    void register_active_transaction(uint64_t transaction_id, uint64_t start_timestamp);
    void unregister_active_transaction(uint64_t transaction_id);
};