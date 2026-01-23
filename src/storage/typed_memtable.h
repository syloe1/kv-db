#pragma once
#include "data_types.h"
#include "versioned_value.h"
#include <map>
#include <vector>
#include <string>
#include <cstdint>
#include <mutex>

namespace kvdb {

// 类型化的版本值
struct TypedVersionedValue {
    uint64_t seq;
    TypedValue value;
    bool is_deleted;
    
    TypedVersionedValue() : seq(0), is_deleted(false) {}
    TypedVersionedValue(uint64_t s, const TypedValue& v, bool deleted = false) 
        : seq(s), value(v), is_deleted(deleted) {}
    
    // 序列化支持
    std::string serialize() const;
    static TypedVersionedValue deserialize(const std::string& data);
};

class TypedMemTableIterator; // 前向声明

class TypedMemTable {
public:
    TypedMemTable() = default;
    ~TypedMemTable() = default;
    
    // 基本操作
    void put(const std::string& key, const TypedValue& value, uint64_t seq);
    bool get(const std::string& key, uint64_t snapshot_seq, TypedValue& value) const;
    void del(const std::string& key, uint64_t seq);
    
    // 类型化操作
    void put_int(const std::string& key, int64_t value, uint64_t seq);
    void put_float(const std::string& key, float value, uint64_t seq);
    void put_double(const std::string& key, double value, uint64_t seq);
    void put_string(const std::string& key, const std::string& value, uint64_t seq);
    void put_timestamp(const std::string& key, const Timestamp& value, uint64_t seq);
    void put_date(const std::string& key, const Date& value, uint64_t seq);
    void put_list(const std::string& key, const List& value, uint64_t seq);
    void put_set(const std::string& key, const Set& value, uint64_t seq);
    void put_map(const std::string& key, const Map& value, uint64_t seq);
    void put_blob(const std::string& key, const Blob& value, uint64_t seq);
    
    // 类型化获取
    bool get_int(const std::string& key, uint64_t snapshot_seq, int64_t& value) const;
    bool get_float(const std::string& key, uint64_t snapshot_seq, float& value) const;
    bool get_double(const std::string& key, uint64_t snapshot_seq, double& value) const;
    bool get_string(const std::string& key, uint64_t snapshot_seq, std::string& value) const;
    bool get_timestamp(const std::string& key, uint64_t snapshot_seq, Timestamp& value) const;
    bool get_date(const std::string& key, uint64_t snapshot_seq, Date& value) const;
    bool get_list(const std::string& key, uint64_t snapshot_seq, List& value) const;
    bool get_set(const std::string& key, uint64_t snapshot_seq, Set& value) const;
    bool get_map(const std::string& key, uint64_t snapshot_seq, Map& value) const;
    bool get_blob(const std::string& key, uint64_t snapshot_seq, Blob& value) const;
    
    // 集合操作
    bool list_append(const std::string& key, const TypedValue& value, uint64_t seq);
    bool list_prepend(const std::string& key, const TypedValue& value, uint64_t seq);
    bool list_remove(const std::string& key, size_t index, uint64_t seq);
    bool list_get(const std::string& key, size_t index, uint64_t snapshot_seq, TypedValue& value) const;
    bool list_set(const std::string& key, size_t index, const TypedValue& value, uint64_t seq);
    bool list_size(const std::string& key, uint64_t snapshot_seq, size_t& size) const;
    
    bool set_add(const std::string& key, const TypedValue& value, uint64_t seq);
    bool set_remove(const std::string& key, const TypedValue& value, uint64_t seq);
    bool set_contains(const std::string& key, const TypedValue& value, uint64_t snapshot_seq) const;
    bool set_size(const std::string& key, uint64_t snapshot_seq, size_t& size) const;
    
    bool map_put(const std::string& key, const std::string& field, const TypedValue& value, uint64_t seq);
    bool map_get(const std::string& key, const std::string& field, uint64_t snapshot_seq, TypedValue& value) const;
    bool map_remove(const std::string& key, const std::string& field, uint64_t seq);
    bool map_contains(const std::string& key, const std::string& field, uint64_t snapshot_seq) const;
    bool map_size(const std::string& key, uint64_t snapshot_seq, size_t& size) const;
    bool map_keys(const std::string& key, uint64_t snapshot_seq, std::vector<std::string>& keys) const;
    
    // 元数据操作
    size_t size() const; // 返回字节数
    size_t count() const; // 返回键数量
    void clear();
    
    // 获取所有版本数据，用于 flush 到 SSTable
    std::map<std::string, std::vector<TypedVersionedValue>> get_all_versions() const;
    
    // 用于迭代器访问
    const std::map<std::string, std::vector<TypedVersionedValue>>& get_table() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return table_;
    }
    
    // 类型检查
    DataType get_key_type(const std::string& key, uint64_t snapshot_seq) const;
    bool key_exists(const std::string& key, uint64_t snapshot_seq) const;
    
    // 范围查询支持
    std::vector<std::pair<std::string, TypedValue>> range_scan(
        const std::string& start_key, 
        const std::string& end_key, 
        uint64_t snapshot_seq,
        size_t limit = 0) const;
    
    // 类型过滤查询
    std::vector<std::pair<std::string, TypedValue>> type_scan(
        DataType type, 
        uint64_t snapshot_seq,
        size_t limit = 0) const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::vector<TypedVersionedValue>> table_;
    size_t size_bytes_ = 0;
    
    // 辅助方法
    TypedValue* get_mutable_value(const std::string& key, uint64_t seq);
    const TypedValue* get_latest_value(const std::string& key, uint64_t snapshot_seq) const;
    void update_size(const TypedValue& old_value, const TypedValue& new_value);
    void add_version(const std::string& key, const TypedVersionedValue& version);
};

} // namespace kvdb