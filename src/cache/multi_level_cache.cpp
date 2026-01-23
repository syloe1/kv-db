#include "multi_level_cache.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cctype>

// AccessPattern 实现
void AccessPattern::record_access(const std::string& key) {
    std::lock_guard<std::mutex> lock(pattern_mutex);
    
    total_reads.fetch_add(1);
    
    if (!last_key.empty()) {
        // 简单的顺序检测：检查key是否是数字递增或字符串相似
        if (is_sequential(last_key, key)) {
            sequential_reads.fetch_add(1);
        } else {
            random_reads.fetch_add(1);
        }
    }
    
    last_key = key;
}

double AccessPattern::get_sequential_ratio() const {
    uint64_t total = sequential_reads.load() + random_reads.load();
    if (total == 0) return 0.0;
    return static_cast<double>(sequential_reads.load()) / total;
}

bool AccessPattern::is_sequential(const std::string& prev, const std::string& curr) {
    // 简单的顺序检测逻辑
    if (prev.length() != curr.length()) return false;
    
    // 检查是否只有最后几个字符不同（如key001, key002）
    size_t diff_count = 0;
    for (size_t i = 0; i < prev.length(); ++i) {
        if (prev[i] != curr[i]) {
            diff_count++;
        }
    }
    
    return diff_count <= 3; // 允许最多3个字符不同
}

// HotDataCache 实现
HotDataCache::HotDataCache(size_t capacity) : capacity_(capacity) {}

std::optional<std::string> HotDataCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        misses_.fetch_add(1);
        return std::nullopt;
    }
    
    hits_.fetch_add(1);
    
    // 更新访问信息
    auto& item = it->second.second;
    item->access_time = std::chrono::steady_clock::now();
    item->access_count++;
    
    // 移到LRU头部
    lru_.splice(lru_.begin(), lru_, it->second.first);
    
    return item->value;
}

void HotDataCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // 更新现有项
        it->second.second->value = value;
        it->second.second->access_time = std::chrono::steady_clock::now();
        lru_.splice(lru_.begin(), lru_, it->second.first);
        return;
    }
    
    // 检查容量
    if (cache_.size() >= capacity_) {
        evict_lru();
    }
    
    // 添加新项
    auto item = std::make_shared<CacheItem>(value);
    item->is_hot = true;
    
    lru_.push_front(key);
    cache_[key] = {lru_.begin(), item};
}

void HotDataCache::promote_from_l2(const std::string& key, const std::string& value) {
    put(key, value); // 直接放入L1，标记为热点数据
}

double HotDataCache::get_hit_rate() const {
    uint64_t total = hits_.load() + misses_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(hits_.load()) / total * 100.0;
}

void HotDataCache::evict_lru() {
    if (lru_.empty()) return;
    
    std::string old_key = lru_.back();
    lru_.pop_back();
    cache_.erase(old_key);
}

// L2BlockCache 实现
L2BlockCache::L2BlockCache(size_t capacity) : capacity_(capacity) {}

std::optional<std::string> L2BlockCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        misses_.fetch_add(1);
        return std::nullopt;
    }
    
    hits_.fetch_add(1);
    
    // 更新访问信息
    auto& item = it->second.second;
    item->access_time = std::chrono::steady_clock::now();
    item->access_count++;
    
    // 移到LRU头部
    lru_.splice(lru_.begin(), lru_, it->second.first);
    
    return item->value;
}

void L2BlockCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // 更新现有项
        it->second.second->value = value;
        it->second.second->access_time = std::chrono::steady_clock::now();
        lru_.splice(lru_.begin(), lru_, it->second.first);
        return;
    }
    
    // 检查容量
    if (cache_.size() >= capacity_) {
        evict_lru();
    }
    
    // 添加新项
    auto item = std::make_shared<CacheItem>(value);
    
    lru_.push_front(key);
    cache_[key] = {lru_.begin(), item};
}

bool L2BlockCache::should_promote_to_l1(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it == cache_.end()) return false;
    
    return it->second.second->access_count >= HOT_THRESHOLD;
}

double L2BlockCache::get_hit_rate() const {
    uint64_t total = hits_.load() + misses_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(hits_.load()) / total * 100.0;
}

void L2BlockCache::evict_lru() {
    if (lru_.empty()) return;
    
    std::string old_key = lru_.back();
    lru_.pop_back();
    cache_.erase(old_key);
}

// Prefetcher 实现
Prefetcher::Prefetcher(size_t max_prefetch_size) : max_prefetch_size_(max_prefetch_size) {}

std::vector<std::string> Prefetcher::get_prefetch_keys(const std::string& current_key, 
                                                       const AccessPattern& pattern) {
    std::vector<std::string> prefetch_keys;
    
    // 如果顺序访问比例高，预读后续key
    if (pattern.get_sequential_ratio() > 0.7) {
        for (size_t i = 1; i <= max_prefetch_size_; ++i) {
            std::string next_key = get_next_sequential_key(current_key);
            if (!next_key.empty()) {
                prefetch_keys.push_back(next_key);
            }
        }
    }
    
    // 基于历史访问序列预读
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = access_sequences_.find(current_key);
    if (it != access_sequences_.end()) {
        for (const auto& next_key : it->second) {
            if (prefetch_keys.size() >= max_prefetch_size_) break;
            prefetch_keys.push_back(next_key);
        }
    }
    
    return prefetch_keys;
}

void Prefetcher::record_access(const std::string& key) {
    // 这里可以记录访问序列，用于预测下一个可能访问的key
    // 简化实现，实际可以使用更复杂的机器学习算法
}

std::string Prefetcher::get_next_sequential_key(const std::string& key) {
    // 简单的顺序key生成逻辑
    // 假设key格式为 prefix + number
    std::string result = key;
    
    // 找到最后的数字部分
    size_t i = result.length();
    while (i > 0 && std::isdigit(result[i-1])) {
        i--;
    }
    
    if (i < result.length()) {
        // 提取数字部分并递增
        std::string prefix = result.substr(0, i);
        std::string number_str = result.substr(i);
        int number = std::stoi(number_str);
        
        std::ostringstream oss;
        oss << prefix << std::setfill('0') << std::setw(number_str.length()) << (number + 1);
        return oss.str();
    }
    
    return ""; // 无法生成下一个key
}

// MultiLevelCache 实现
MultiLevelCache::MultiLevelCache(size_t l1_capacity, size_t l2_capacity)
    : l1_cache_(std::make_unique<HotDataCache>(l1_capacity)),
      l2_cache_(std::make_unique<L2BlockCache>(l2_capacity)),
      prefetcher_(std::make_unique<Prefetcher>()) {
    
    // 启动后台调整线程
    adjustment_thread_ = std::thread(&MultiLevelCache::adjustment_worker, this);
}

MultiLevelCache::~MultiLevelCache() {
    stop_adjustment_.store(true);
    if (adjustment_thread_.joinable()) {
        adjustment_thread_.join();
    }
}

std::optional<std::string> MultiLevelCache::get(const std::string& key) {
    total_requests_.fetch_add(1);
    access_pattern_.record_access(key);
    
    // 1. 先查L1缓存
    auto result = l1_cache_->get(key);
    if (result.has_value()) {
        l1_hits_.fetch_add(1);
        return result;
    }
    
    // 2. 查L2缓存
    result = l2_cache_->get(key);
    if (result.has_value()) {
        l2_hits_.fetch_add(1);
        
        // 检查是否应该提升到L1
        if (l2_cache_->should_promote_to_l1(key)) {
            l1_cache_->promote_from_l2(key, result.value());
        }
        
        return result;
    }
    
    return std::nullopt;
}

void MultiLevelCache::put(const std::string& key, const std::string& value) {
    // 新数据直接放入L2，等待访问频率决定是否提升到L1
    l2_cache_->put(key, value);
}

void MultiLevelCache::prefetch(const std::vector<std::string>& keys, 
                              std::function<std::optional<std::string>(const std::string&)> loader) {
    for (const auto& key : keys) {
        // 检查是否已在缓存中
        if (get(key).has_value()) {
            prefetch_hits_.fetch_add(1);
            continue;
        }
        
        // 从存储加载数据
        auto value = loader(key);
        if (value.has_value()) {
            put(key, value.value());
        }
    }
}

void MultiLevelCache::warm_cache(const std::vector<std::pair<std::string, std::string>>& hot_data) {
    std::cout << "[MultiLevelCache] 开始缓存预热，加载 " << hot_data.size() << " 个热点数据\n";
    
    for (const auto& [key, value] : hot_data) {
        l1_cache_->put(key, value); // 直接放入L1作为热点数据
    }
    
    std::cout << "[MultiLevelCache] 缓存预热完成\n";
}

MultiLevelCache::CacheStats MultiLevelCache::get_stats() const {
    CacheStats stats;
    
    uint64_t total = total_requests_.load();
    uint64_t l1_hits = l1_hits_.load();
    uint64_t l2_hits = l2_hits_.load();
    
    stats.l1_hit_rate = l1_cache_->get_hit_rate();
    stats.l2_hit_rate = l2_cache_->get_hit_rate();
    stats.overall_hit_rate = total > 0 ? static_cast<double>(l1_hits + l2_hits) / total * 100.0 : 0.0;
    stats.l1_size = l1_cache_->size();
    stats.l2_size = l2_cache_->size();
    stats.sequential_ratio = access_pattern_.get_sequential_ratio();
    stats.total_reads = total;
    stats.prefetch_hits = prefetch_hits_.load();
    
    return stats;
}

void MultiLevelCache::print_stats() const {
    auto stats = get_stats();
    
    std::cout << "\n=== 多级缓存统计 ===\n";
    std::cout << "L1 缓存命中率: " << stats.l1_hit_rate << "%\n";
    std::cout << "L2 缓存命中率: " << stats.l2_hit_rate << "%\n";
    std::cout << "总体命中率: " << stats.overall_hit_rate << "%\n";
    std::cout << "L1 缓存大小: " << stats.l1_size << "\n";
    std::cout << "L2 缓存大小: " << stats.l2_size << "\n";
    std::cout << "顺序访问比例: " << (stats.sequential_ratio * 100) << "%\n";
    std::cout << "总读取次数: " << stats.total_reads << "\n";
    std::cout << "预读命中次数: " << stats.prefetch_hits << "\n";
    std::cout << "==================\n\n";
}

void MultiLevelCache::adjust_strategy() {
    auto stats = get_stats();
    
    // 根据访问模式调整策略
    if (stats.sequential_ratio > 0.8) {
        // 高顺序访问，增加预读
        std::cout << "[MultiLevelCache] 检测到高顺序访问模式，优化预读策略\n";
    } else if (stats.l1_hit_rate < 50.0 && stats.l2_hit_rate > 80.0) {
        // L1命中率低但L2命中率高，可能需要调整L1容量或提升策略
        std::cout << "[MultiLevelCache] L1命中率偏低，考虑调整热点数据识别策略\n";
    }
}

void MultiLevelCache::adjustment_worker() {
    while (!stop_adjustment_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(30)); // 每30秒调整一次
        
        if (!stop_adjustment_.load()) {
            adjust_strategy();
        }
    }
}