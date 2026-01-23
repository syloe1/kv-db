#pragma once
#include "index_types.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <memory>
#include <shared_mutex>

class SecondaryIndex {
public:
    explicit SecondaryIndex(const std::string& name, const std::string& field, bool unique = false);
    ~SecondaryIndex();
    
    // 索引操作
    void insert(const std::string& indexed_value, const std::string& primary_key);
    void remove(const std::string& indexed_value, const std::string& primary_key);
    void update(const std::string& old_value, const std::string& new_value, const std::string& primary_key);
    
    // 查询操作
    std::vector<std::string> exact_lookup(const std::string& value);
    std::vector<std::string> range_lookup(const std::string& start, const std::string& end);
    std::vector<std::string> prefix_lookup(const std::string& prefix);
    
    // 统计信息
    size_t size() const;
    size_t unique_values() const;
    double selectivity() const;
    size_t memory_usage() const;
    
    // 序列化
    void serialize(std::ostream& out) const;
    void deserialize(std::istream& in);
    
    // 清空索引
    void clear();
    
    // 获取所有值
    std::vector<std::string> get_all_values() const;
    
    const std::string& get_name() const { return name_; }
    const std::string& get_field() const { return field_; }
    bool is_unique() const { return is_unique_; }
    
private:
    std::string name_;
    std::string field_;
    bool is_unique_;
    
    // 索引数据结构: indexed_value -> set<primary_key>
    std::map<std::string, std::set<std::string>> index_map_;
    
    // 读写锁
    mutable std::shared_mutex mutex_;
    
    // 统计信息
    mutable size_t total_entries_;
    mutable bool stats_dirty_;
    
    // 辅助方法
    void update_stats() const;
    bool validate_unique_constraint(const std::string& value, const std::string& key) const;
};