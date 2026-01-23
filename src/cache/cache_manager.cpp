#include "cache_manager.h"
#include <iostream>

CacheManager::CacheManager(CacheType type, size_t l1_capacity, size_t l2_capacity)
    : current_type_(type), l1_capacity_(l1_capacity), l2_capacity_(l2_capacity) {
    
    switch (type) {
        case CacheType::MULTI_LEVEL_CACHE:
            multi_level_cache_ = std::make_unique<MultiLevelCache>(l1_capacity, l2_capacity);
            std::cout << "[CacheManager] 初始化多级缓存系统 (L1:" << l1_capacity 
                      << ", L2:" << l2_capacity << ")\n";
            break;
            
        case CacheType::LEGACY_BLOCK_CACHE:
            legacy_cache_ = std::make_unique<BlockCache>(l2_capacity);
            std::cout << "[CacheManager] 初始化传统块缓存 (容量:" << l2_capacity << ")\n";
            break;
    }
}

std::optional<std::string> CacheManager::get(const std::string& key) {
    switch (current_type_) {
        case CacheType::MULTI_LEVEL_CACHE:
            return multi_level_cache_->get(key);
            
        case CacheType::LEGACY_BLOCK_CACHE:
            return legacy_cache_->get(key);
    }
    return std::nullopt;
}

void CacheManager::put(const std::string& key, const std::string& value) {
    switch (current_type_) {
        case CacheType::MULTI_LEVEL_CACHE:
            multi_level_cache_->put(key, value);
            break;
            
        case CacheType::LEGACY_BLOCK_CACHE:
            legacy_cache_->put(key, value);
            break;
    }
}

void CacheManager::prefetch(const std::vector<std::string>& keys, 
                           std::function<std::optional<std::string>(const std::string&)> loader) {
    if (current_type_ == CacheType::MULTI_LEVEL_CACHE) {
        multi_level_cache_->prefetch(keys, loader);
    } else {
        std::cout << "[CacheManager] 预读功能仅在多级缓存模式下可用\n";
    }
}

void CacheManager::warm_cache(const std::vector<std::pair<std::string, std::string>>& hot_data) {
    if (current_type_ == CacheType::MULTI_LEVEL_CACHE) {
        multi_level_cache_->warm_cache(hot_data);
    } else {
        std::cout << "[CacheManager] 缓存预热功能仅在多级缓存模式下可用\n";
        // 对于传统缓存，简单地put所有数据
        for (const auto& [key, value] : hot_data) {
            legacy_cache_->put(key, value);
        }
    }
}

double CacheManager::get_hit_rate() const {
    switch (current_type_) {
        case CacheType::MULTI_LEVEL_CACHE: {
            auto stats = multi_level_cache_->get_stats();
            return stats.overall_hit_rate;
        }
        case CacheType::LEGACY_BLOCK_CACHE:
            return legacy_cache_->get_hit_rate();
    }
    return 0.0;
}

void CacheManager::print_stats() const {
    std::cout << "\n=== 缓存管理器统计 ===\n";
    std::cout << "当前缓存类型: " << 
        (current_type_ == CacheType::MULTI_LEVEL_CACHE ? "多级缓存" : "传统块缓存") << "\n";
    
    switch (current_type_) {
        case CacheType::MULTI_LEVEL_CACHE:
            multi_level_cache_->print_stats();
            break;
            
        case CacheType::LEGACY_BLOCK_CACHE:
            std::cout << "缓存命中率: " << legacy_cache_->get_hit_rate() << "%\n";
            std::cout << "==================\n\n";
            break;
    }
}

void CacheManager::switch_to_multi_level() {
    if (current_type_ == CacheType::MULTI_LEVEL_CACHE) {
        std::cout << "[CacheManager] 已经是多级缓存模式\n";
        return;
    }
    
    std::cout << "[CacheManager] 切换到多级缓存模式\n";
    
    // 清理旧缓存
    legacy_cache_.reset();
    
    // 创建新缓存
    multi_level_cache_ = std::make_unique<MultiLevelCache>(l1_capacity_, l2_capacity_);
    current_type_ = CacheType::MULTI_LEVEL_CACHE;
}

void CacheManager::switch_to_legacy() {
    if (current_type_ == CacheType::LEGACY_BLOCK_CACHE) {
        std::cout << "[CacheManager] 已经是传统缓存模式\n";
        return;
    }
    
    std::cout << "[CacheManager] 切换到传统块缓存模式\n";
    
    // 清理旧缓存
    multi_level_cache_.reset();
    
    // 创建新缓存
    legacy_cache_ = std::make_unique<BlockCache>(l2_capacity_);
    current_type_ = CacheType::LEGACY_BLOCK_CACHE;
}

BlockCache& CacheManager::get_block_cache() {
    if (current_type_ == CacheType::LEGACY_BLOCK_CACHE && legacy_cache_) {
        return *legacy_cache_;
    } else {
        // 如果是多级缓存，我们需要创建一个临时的BlockCache或者抛出异常
        // 为了简单起见，我们切换到legacy模式
        if (!legacy_cache_) {
            legacy_cache_ = std::make_unique<BlockCache>(l2_capacity_);
        }
        return *legacy_cache_;
    }
}