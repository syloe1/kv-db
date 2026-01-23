#pragma once
#include "kv_db.h"
#include "log/wal.h"
#include "cache/cache_manager.h"
#include "cache/cache_adapter.h"
#include "sstable/sstable_meta.h"
#include "version/version_set.h"
#include "snapshot/snapshot.h"
#include "snapshot/snapshot_manager.h"
#include "iterator/iterator.h"
#include "iterator/concurrent_iterator.h"
#include "compaction/compaction_strategy.h"
#include "index/index_manager.h"
#include <vector>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <mutex>
#include <memory>

class KVDB {
public:
    explicit KVDB(const std::string& wal_file);
    ~KVDB();
    
    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    bool get(const std::string& key, const Snapshot& snapshot, std::string& value);
    bool del(const std::string& key);
    
    Snapshot get_snapshot();
    void release_snapshot(const Snapshot& snapshot);
    std::unique_ptr<Iterator> new_iterator(const Snapshot& snapshot);
    std::unique_ptr<Iterator> new_prefix_iterator(const Snapshot& snapshot, const std::string& prefix);
    
    // 并发安全的迭代器
    std::shared_ptr<ConcurrentIterator> new_concurrent_iterator(const Snapshot& snapshot);
    std::shared_ptr<ConcurrentIterator> new_concurrent_prefix_iterator(const Snapshot& snapshot, const std::string& prefix);

    void flush();
    void compact(); //手动触发Compaction
    
    // 压缩策略管理
    void set_compaction_strategy(CompactionStrategyType type);
    CompactionStrategyType get_compaction_strategy() const;
    const CompactionStats& get_compaction_stats() const;
    
    // 缓存管理
    void enable_multi_level_cache();
    void enable_legacy_cache();
    void warm_cache_with_hot_data(const std::vector<std::pair<std::string, std::string>>& hot_data);
    void print_cache_stats() const;
    
    // REPL 支持方法
    size_t get_memtable_size() const;
    size_t get_wal_size() const;
    double get_cache_hit_rate() const;
    void print_lsm_structure() const;
    
    // 索引管理
    IndexManager& get_index_manager() { return *index_manager_; }
    const IndexManager& get_index_manager() const { return *index_manager_; }
    
    // 索引操作
    bool create_secondary_index(const std::string& name, const std::string& field, bool unique = false);
    bool create_composite_index(const std::string& name, const std::vector<std::string>& fields);
    bool create_fulltext_index(const std::string& name, const std::string& field);
    bool create_inverted_index(const std::string& name, const std::string& field);
    bool drop_index(const std::string& name);
    std::vector<IndexMetadata> list_indexes();

private:
    // 读写隔离相关
    void begin_write_operation();
    void end_write_operation();
    mutable std::shared_mutex db_rw_mutex_; // 数据库级别的读写锁
    
    // 压缩策略
    std::unique_ptr<CompactionStrategy> compaction_strategy_;
    mutable std::mutex compaction_strategy_mutex_;
    static constexpr size_t MEMTABLE_LIMIT = 4 * 1024 * 1024; // 4MB
    static constexpr int MAX_LEVEL = 4;
    static constexpr int LEVEL_LIMITS[MAX_LEVEL] = {4, 8, 16, 32};

    struct Level {
        std::vector<SSTableMeta> sstables;
        mutable std::mutex mutex;
        
        Level() = default;
        Level(const Level& other) {
            std::lock_guard<std::mutex> lock(other.mutex);
            sstables = other.sstables;
        }
        Level& operator=(const Level& other) {
            if (this != &other) {
                std::lock_guard<std::mutex> lock_this(mutex, std::adopt_lock);
                std::lock_guard<std::mutex> lock_other(other.mutex, std::adopt_lock);
                sstables = other.sstables;
            }
            return *this;
        }
    };

    uint64_t next_seq();
    void request_flush();
    void flush_worker();
    void compact_worker();
    bool need_compaction() const;
    void request_compaction();
    void compact_level(int level);
    void execute_compaction_task(std::unique_ptr<CompactionTask> task);
    std::vector<SSTableMeta> get_overlapping_sstables(int level, const SSTableMeta& input);
    void update_level_metadata(int level, const std::vector<SSTableMeta>& old_files);

    MemTable memtable_;
    WAL wal_;
    std::unique_ptr<CacheManager> cache_manager_;
    int file_id_ = 0;
    std::vector<Level> levels_;
    VersionSet version_set_{MAX_LEVEL};
    SnapshotManager snapshot_manager_;
    std::atomic<uint64_t> seq_{0}; // 全局序列号

    // Background thread management
    std::thread bg_flush_thread_;
    std::thread bg_compact_thread_;
    std::condition_variable flush_cv_;
    std::condition_variable compact_cv_;
    std::mutex flush_mutex_;
    std::mutex compact_mutex_;
    std::atomic<bool> flush_requested_{false};
    std::atomic<bool> compaction_requested_{false};
    std::atomic<bool> stop_{false};
    
    // 索引管理器
    std::unique_ptr<IndexManager> index_manager_;
};
