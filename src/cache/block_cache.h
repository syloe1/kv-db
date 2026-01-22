#pragma once
#include <unordered_map>
#include <list>
#include <string>
#include <optional>

class BlockCache {
public:
    explicit BlockCache(size_t capacity);

    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);
    
    double get_hit_rate() const;

private:
    size_t capacity_;
    mutable size_t hits_ = 0;
    mutable size_t misses_ = 0;

    // LRU: list front = most recent
    std::list<std::string> lru_;
    std::unordered_map<
        std::string,
        std::pair<std::list<std::string>::iterator, std::string>
    > cache_;
};
