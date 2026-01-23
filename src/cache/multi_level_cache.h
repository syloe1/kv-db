#pragma once
#include <unordered_map>
#include <list>
#include <string>
#include <optional>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <thread>
#include <functional>

// 缓存项元数据
struct CacheItem {
    std::string value;
    std::chrono::steady_clock::time_point access_time;
    std::chrono::steady_clock::time_point create_time;
    uint32_t access_count;
    uint32_t size;
    bool is_hot;
    
    CacheItem(const std::string& v) 
        : value(v), access_time(std::chrono::steady_clock::now()),
          create_time(std::chrono::steady_clock::now()),
          access_count(1), size(v.size()), is_hot(false) {}
};

// 访问模式统计
struct AccessPattern {
    std::atomic<uint64_t> sequential_reads{0};
    std::atomic<uint64_t> random_reads{0};
    std::atomic<uint64_t> total_reads{0};
    std::string last_key;
    std::mutex pattern_mutex;
    
    void record_access(const std::string& key);
    double get_sequential_ratio() const;
    
private:
    bool is_sequential(const std::string& prev, const std::string& curr);
};

// L1 缓存：热点数据缓存
class HotDataCache {
public:
    explicit HotDataCache(size_t capacity);
    
    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);
    void promote_from_l2(const std::string& key, const std::string& value);
    
    double get_hit_rate() const;
    size_t size() const { return cache_.size(); }
    
private:
    size_t capacity_;
    mutable std::atomic<uint64_t> hits_{0};
    mutable std::atomic<uint64_t> misses_{0};
    
    std::list<std::string> lru_;
    std::unordered_map<std::string, std::pair<std::list<std::string>::iterator, std::shared_ptr<CacheItem>>> cache_;
    mutable std::mutex mutex_;
    
    void evict_lru();
};

// L2 缓存：块缓存（重命名避免冲突）
class L2BlockCache {
public:
    explicit L2BlockCache(size_t capacity);
    
    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);
    bool should_promote_to_l1(const std::string& key) const;
    
    double get_hit_rate() const;
    size_t size() const { return cache_.size(); }
    
private:
    size_t capacity_;
    mutable std::atomic<uint64_t> hits_{0};
    mutable std::atomic<uint64_t> misses_{0};
    
    std::list<std::string> lru_;
    std::unordered_map<std::string, std::pair<std::list<std::string>::iterator, std::shared_ptr<CacheItem>>> cache_;
    mutable std::mutex mutex_;
    
    void evict_lru();
    static constexpr uint32_t HOT_THRESHOLD = 3; // 访问3次以上认为是热点
};

// 预读器
class Prefetcher {
public:
    explicit Prefetcher(size_t max_prefetch_size = 4);
    
    std::vector<std::string> get_prefetch_keys(const std::string& current_key, 
                                               const AccessPattern& pattern);
    void record_access(const std::string& key);
    
private:
    size_t max_prefetch_size_;
    std::unordered_map<std::string, std::vector<std::string>> access_sequences_;
    std::mutex mutex_;
    
    std::string get_next_sequential_key(const std::string& key);
};

// 多级缓存管理器
class MultiLevelCache {
public:
    MultiLevelCache(size_t l1_capacity = 1024,      // 1K 热点数据
                   size_t l2_capacity = 8192);      // 8K 块缓存
    ~MultiLevelCache();
    
    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);
    
    // 预读接口
    void prefetch(const std::vector<std::string>& keys, 
                  std::function<std::optional<std::string>(const std::string&)> loader);
    
    // 缓存预热
    void warm_cache(const std::vector<std::pair<std::string, std::string>>& hot_data);
    
    // 统计信息
    struct CacheStats {
        double l1_hit_rate;
        double l2_hit_rate;
        double overall_hit_rate;
        size_t l1_size;
        size_t l2_size;
        double sequential_ratio;
        uint64_t total_reads;
        uint64_t prefetch_hits;
    };
    
    CacheStats get_stats() const;
    void print_stats() const;
    
    // 自适应调整
    void adjust_strategy();
    
private:
    std::unique_ptr<HotDataCache> l1_cache_;
    std::unique_ptr<L2BlockCache> l2_cache_;
    std::unique_ptr<Prefetcher> prefetcher_;
    AccessPattern access_pattern_;
    
    mutable std::atomic<uint64_t> total_requests_{0};
    mutable std::atomic<uint64_t> l1_hits_{0};
    mutable std::atomic<uint64_t> l2_hits_{0};
    mutable std::atomic<uint64_t> prefetch_hits_{0};
    
    // 后台调整线程
    std::thread adjustment_thread_;
    std::atomic<bool> stop_adjustment_{false};
    
    void adjustment_worker();
};