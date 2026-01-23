#pragma once
#include "kv_db.h"
#include "../storage/typed_memtable.h"
#include "../storage/data_types.h"
#include <memory>

namespace kvdb {

class TypedKVDB : public KVDB {
public:
    explicit TypedKVDB(const std::string& wal_file);
    ~TypedKVDB() = default;
    
    // 基本类型化操作
    bool put_typed(const std::string& key, const TypedValue& value);
    bool get_typed(const std::string& key, TypedValue& value);
    bool get_typed(const std::string& key, const Snapshot& snapshot, TypedValue& value);
    
    // 数值类型操作
    bool put_int(const std::string& key, int64_t value);
    bool put_float(const std::string& key, float value);
    bool put_double(const std::string& key, double value);
    bool get_int(const std::string& key, int64_t& value);
    bool get_float(const std::string& key, float& value);
    bool get_double(const std::string& key, double& value);
    
    // 字符串操作
    bool put_string(const std::string& key, const std::string& value);
    bool get_string(const std::string& key, std::string& value);
    
    // 时间类型操作
    bool put_timestamp(const std::string& key, const Timestamp& value);
    bool put_date(const std::string& key, const Date& value);
    bool get_timestamp(const std::string& key, Timestamp& value);
    bool get_date(const std::string& key, Date& value);
    
    // 时间字符串操作（便利方法）
    bool put_timestamp_str(const std::string& key, const std::string& timestamp_str);
    bool put_date_str(const std::string& key, const std::string& date_str);
    bool get_timestamp_str(const std::string& key, std::string& timestamp_str);
    bool get_date_str(const std::string& key, std::string& date_str);
    
    // 集合类型操作
    bool put_list(const std::string& key, const List& value);
    bool put_set(const std::string& key, const Set& value);
    bool put_map(const std::string& key, const Map& value);
    bool get_list(const std::string& key, List& value);
    bool get_set(const std::string& key, Set& value);
    bool get_map(const std::string& key, Map& value);
    
    // 二进制类型操作
    bool put_blob(const std::string& key, const Blob& value);
    bool get_blob(const std::string& key, Blob& value);
    
    // 便利方法：从文件读取二进制数据
    bool put_blob_from_file(const std::string& key, const std::string& file_path);
    bool get_blob_to_file(const std::string& key, const std::string& file_path);
    
    // 列表操作
    bool list_append(const std::string& key, const TypedValue& value);
    bool list_prepend(const std::string& key, const TypedValue& value);
    bool list_remove(const std::string& key, size_t index);
    bool list_get(const std::string& key, size_t index, TypedValue& value);
    bool list_set(const std::string& key, size_t index, const TypedValue& value);
    bool list_size(const std::string& key, size_t& size);
    bool list_clear(const std::string& key);
    
    // 集合操作
    bool set_add(const std::string& key, const TypedValue& value);
    bool set_remove(const std::string& key, const TypedValue& value);
    bool set_contains(const std::string& key, const TypedValue& value);
    bool set_size(const std::string& key, size_t& size);
    bool set_clear(const std::string& key);
    bool set_union(const std::string& key1, const std::string& key2, const std::string& result_key);
    bool set_intersection(const std::string& key1, const std::string& key2, const std::string& result_key);
    bool set_difference(const std::string& key1, const std::string& key2, const std::string& result_key);
    
    // 映射操作
    bool map_put(const std::string& key, const std::string& field, const TypedValue& value);
    bool map_get(const std::string& key, const std::string& field, TypedValue& value);
    bool map_remove(const std::string& key, const std::string& field);
    bool map_contains(const std::string& key, const std::string& field);
    bool map_size(const std::string& key, size_t& size);
    bool map_keys(const std::string& key, std::vector<std::string>& keys);
    bool map_values(const std::string& key, std::vector<TypedValue>& values);
    bool map_clear(const std::string& key);
    
    // 类型检查和转换
    DataType get_key_type(const std::string& key);
    bool key_exists_typed(const std::string& key);
    bool convert_key_type(const std::string& key, DataType target_type);
    
    // 范围查询
    std::vector<std::pair<std::string, TypedValue>> range_scan_typed(
        const std::string& start_key, 
        const std::string& end_key = "",
        size_t limit = 0);
    
    // 类型过滤查询
    std::vector<std::pair<std::string, TypedValue>> type_scan(
        DataType type, 
        size_t limit = 0);
    
    // 数值范围查询
    std::vector<std::pair<std::string, TypedValue>> numeric_range_scan(
        double min_value, 
        double max_value,
        size_t limit = 0);
    
    // 时间范围查询
    std::vector<std::pair<std::string, TypedValue>> timestamp_range_scan(
        const Timestamp& start_time,
        const Timestamp& end_time,
        size_t limit = 0);
    
    std::vector<std::pair<std::string, TypedValue>> date_range_scan(
        const Date& start_date,
        const Date& end_date,
        size_t limit = 0);
    
    // 统计信息
    struct TypeStats {
        size_t null_count = 0;
        size_t int_count = 0;
        size_t float_count = 0;
        size_t double_count = 0;
        size_t string_count = 0;
        size_t timestamp_count = 0;
        size_t date_count = 0;
        size_t list_count = 0;
        size_t set_count = 0;
        size_t map_count = 0;
        size_t blob_count = 0;
        
        size_t total_count() const {
            return null_count + int_count + float_count + double_count + 
                   string_count + timestamp_count + date_count + 
                   list_count + set_count + map_count + blob_count;
        }
    };
    
    TypeStats get_type_statistics();
    
    // 数据导入导出
    bool export_to_json(const std::string& file_path);
    bool import_from_json(const std::string& file_path);
    
    // 批量操作
    struct TypedOperation {
        enum Type { PUT, DELETE } type;
        std::string key;
        TypedValue value;
        
        TypedOperation(Type t, const std::string& k, const TypedValue& v = TypedValue())
            : type(t), key(k), value(v) {}
    };
    
    bool batch_execute(const std::vector<TypedOperation>& operations);
    
    // 事务支持（简单版本）
    class TypedTransaction {
    public:
        explicit TypedTransaction(TypedKVDB* db);
        ~TypedTransaction();
        
        bool put_typed(const std::string& key, const TypedValue& value);
        bool get_typed(const std::string& key, TypedValue& value);
        bool del(const std::string& key);
        
        bool commit();
        void rollback();
        
    private:
        TypedKVDB* db_;
        std::vector<TypedOperation> operations_;
        bool committed_;
        bool rolled_back_;
    };
    
    std::unique_ptr<TypedTransaction> begin_transaction();

private:
    std::unique_ptr<TypedMemTable> typed_memtable_;
    
    // 辅助方法
    bool flush_typed_memtable();
    TypedValue convert_string_to_typed_value(const std::string& value, DataType type);
    std::string convert_typed_value_to_string(const TypedValue& value);
};

} // namespace kvdb