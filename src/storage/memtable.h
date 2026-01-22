#pragma once
#include <map>
#include <string>
<<<<<<< HEAD
#include <vector>
#include <cstdint>
#include "storage/versioned_value.h"

class MemTableIterator; // 前向声明

class MemTable {
public:
    void put(const std::string& key, const std::string& value, uint64_t seq);
    bool get(const std::string& key, uint64_t snapshot_seq, std::string& value) const;
    void del(const std::string& key, uint64_t seq);

    size_t size() const; // Returns size in bytes
    // 返回所有版本的数据，用于 flush 到 SSTable
    std::map<std::string, std::vector<VersionedValue>> get_all_versions() const;
    void clear();
    
    // 用于 Iterator 访问
    const std::map<std::string, std::vector<VersionedValue>>& get_table() const {
        return table_;
    }
    
private:
    size_t size_bytes_ = 0; // Tracks memory usage
    std::map<std::string, std::vector<VersionedValue>> table_;
};
=======

class MemTable {
public:
    void put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value); 
    void del(const std::string& key);
private:
    std::map<std::string, std::string> table_;
};
>>>>>>> cc24aa4eae4edea13c40a5b76ae3281181c6a76a
