#include "block_cache.h"

BlockCache::BlockCache(size_t capacity)
    : capacity_(capacity) {}

std::optional<std::string> BlockCache::get(const std::string& key) {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        misses_++;
        return std::nullopt;
    }

    hits_++;
    // 移到 LRU 头部
    lru_.splice(lru_.begin(), lru_, it->second.first);
    return it->second.second;
}

double BlockCache::get_hit_rate() const {
    size_t total = hits_ + misses_;
    if (total == 0) return 0.0;
    return (double)hits_ / total * 100.0;
}

void BlockCache::put(const std::string& key, const std::string& value) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // 更新
        it->second.second = value;
        lru_.splice(lru_.begin(), lru_, it->second.first);
        return;
    }

    if (cache_.size() >= capacity_) {
        // 淘汰 LRU 尾部
        auto old = lru_.back();
        lru_.pop_back();
        cache_.erase(old);
    }

    lru_.push_front(key);
    cache_[key] = {lru_.begin(), value};
}
