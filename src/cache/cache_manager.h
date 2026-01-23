#pragma once
#include "multi_level_cache.h"
#include "block_cache.h"
#include <memory>
#include <functional>

// 缓存管理器：统一管理新旧缓存系统
class CacheManager {
public:
    enum class CacheType {
        LEGACY_BLOCK_CACHE,    // 原有的Block Cache
        MULTI_LEVEL_CACHE      // 新的多级缓存
    };
    
    explicit CacheManager(CacheType type = CacheType::MULTI_LEVEL_CACHE,
                         size_t l1_capacity = 1024,
                         size_t l2_capacity = 8192);
    
    // 统一的缓存接口
    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);
    
    // 高级功能（仅多级缓存支持）
    void prefetch(const std::vector<std::string>& keys, 
                  std::function<std::optional<std::string>(const std::string&)> loader);
    void warm_cache(const std::vector<std::pair<std::string, std::string>>& hot_data);
    
    // 统计信息
    double get_hit_rate() const;
    void print_stats() const;
    
    // 缓存类型切换
    void switch_to_multi_level();
    void switch_to_legacy();
    
    CacheType get_cache_type() const { return current_type_; }
    
private:
    CacheType current_type_;
    
    // 缓存实例
    std::unique_ptr<MultiLevelCache> multi_level_cache_;
    std::unique_ptr<BlockCache> legacy_cache_;
    
    size_t l1_capacity_;
    size_t l2_capacity_;
};