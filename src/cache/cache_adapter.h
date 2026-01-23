#pragma once
#include "cache_manager.h"
#include "block_cache.h"
#include <optional>
#include <string>

// 缓存适配器：让CacheManager兼容BlockCache接口
class CacheAdapter {
public:
    explicit CacheAdapter(CacheManager& cache_manager) 
        : cache_manager_(cache_manager) {}
    
    std::optional<std::string> get(const std::string& key) {
        return cache_manager_.get(key);
    }
    
    void put(const std::string& key, const std::string& value) {
        cache_manager_.put(key, value);
    }
    
    double get_hit_rate() const {
        return cache_manager_.get_hit_rate();
    }
    
private:
    CacheManager& cache_manager_;
};