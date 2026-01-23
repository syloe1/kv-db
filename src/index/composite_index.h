#pragma once
#include "index_types.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <memory>
#include <shared_mutex>

class CompositeIndex {
public:
    explicit CompositeIndex(const std::string& name, const std::vector<std::string>& fields);
    ~CompositeIndex();
    
    // 索引操作
    void insert(const std::vector<std::string>& indexed_values, const std::string& primary_key);
    void remove(const std::vector<std::string>& indexed_values, const std::string& primary_key);
    void update(const std::vector<std::string>& old_values, 
                const std::vector<std::string>& new_values, 
                const std::string& primary_key);
    
    // 查询操作
    std::vector<std::string> exact_lookup(const std::vector<std::string>& values);
    std::vector<std::string> prefix_lookup(const std::vector<std::string>& prefix_values);
    std::vector<std::string> range_lookup(const std::vector<std::string>& start_values,
                                         const std::vector<std::string>& end_values);
    
    // 部分匹配查询（只匹配前几个字段）
    std::vector<std::string> partial_lookup(const std::vector<std::string>& partial_values);
    
    // 统计信息
    size_t size() const;
    size_t unique_combinations() const;
    double selectivity() const;
    size_t memory_usage() const;
    
    // 序列化
    void serialize(std::ostream& out) const;
    void deserialize(std::istream& in);
    
    // 清空索引
    void clear();
    
    const std::string& get_name() const { return name_; }
    const std::vector<std::string>& get_fields() const { return fields_; }
    size_t get_field_count() const { return fields_.size(); }
    
private:
    std::string name_;
    std::vector<std::string> fields_;
    
    // 复合索引数据结构: composite_key -> set<primary_key>
    // composite_key 格式: "field1_value|field2_value|field3_value"
    std::map<std::string, std::set<std::string>> index_map_;
    
    // 读写锁
    mutable std::shared_mutex mutex_;
    
    // 统计信息
    mutable size_t total_entries_;
    mutable bool stats_dirty_;
    
    // 辅助方法
    std::string create_composite_key(const std::vector<std::string>& values) const;
    std::vector<std::string> parse_composite_key(const std::string& composite_key) const;
    void update_stats() const;
    bool validate_field_count(const std::vector<std::string>& values) const;
    
    // 分隔符
    static const char FIELD_SEPARATOR = '|';
};