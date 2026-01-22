#pragma once
#include <string>
#include <utility>

struct SSTableMeta {
    std::string filename;
    std::string min_key;
    std::string max_key;
    size_t file_size;
    
    SSTableMeta(const std::string& filename, 
                const std::string& min_key, 
                const std::string& max_key, 
                size_t file_size)
        : filename(filename), min_key(min_key), max_key(max_key), file_size(file_size) {}
    
    bool contains_key(const std::string& key) const {
        return key >= min_key && key <= max_key;
    }
    
    bool overlaps_with(const SSTableMeta& other) const {
        return !(max_key < other.min_key || min_key > other.max_key);
    }
};