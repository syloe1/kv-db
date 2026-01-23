#include "typed_kv_db.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace kvdb {

TypedKVDB::TypedKVDB(const std::string& wal_file) 
    : KVDB(wal_file), typed_memtable_(std::make_unique<TypedMemTable>()) {
}

// 基本类型化操作
bool TypedKVDB::put_typed(const std::string& key, const TypedValue& value) {
    uint64_t seq = next_seq();
    typed_memtable_->put(key, value, seq);
    
    // 同时写入原始 KVDB 以保持兼容性
    std::string serialized = value.serialize();
    return put(key, serialized);
}

bool TypedKVDB::get_typed(const std::string& key, TypedValue& value) {
    Snapshot snapshot = get_snapshot();
    bool result = get_typed(key, snapshot, value);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::get_typed(const std::string& key, const Snapshot& snapshot, TypedValue& value) {
    return typed_memtable_->get(key, snapshot.seq, value);
}

// 数值类型操作
bool TypedKVDB::put_int(const std::string& key, int64_t value) {
    return put_typed(key, TypedValue(value));
}

bool TypedKVDB::put_float(const std::string& key, float value) {
    return put_typed(key, TypedValue(value));
}

bool TypedKVDB::put_double(const std::string& key, double value) {
    return put_typed(key, TypedValue(value));
}

bool TypedKVDB::get_int(const std::string& key, int64_t& value) {
    TypedValue typed_value;
    if (get_typed(key, typed_value) && typed_value.is_int()) {
        value = typed_value.as_int();
        return true;
    }
    return false;
}

bool TypedKVDB::get_float(const std::string& key, float& value) {
    TypedValue typed_value;
    if (get_typed(key, typed_value) && typed_value.is_float()) {
        value = typed_value.as_float();
        return true;
    }
    return false;
}

bool TypedKVDB::get_double(const std::string& key, double& value) {
    TypedValue typed_value;
    if (get_typed(key, typed_value) && typed_value.is_double()) {
        value = typed_value.as_double();
        return true;
    }
    return false;
}

// 字符串操作
bool TypedKVDB::put_string(const std::string& key, const std::string& value) {
    return put_typed(key, TypedValue(value));
}

bool TypedKVDB::get_string(const std::string& key, std::string& value) {
    TypedValue typed_value;
    if (get_typed(key, typed_value) && typed_value.is_string()) {
        value = typed_value.as_string();
        return true;
    }
    return false;
}

// 时间类型操作
bool TypedKVDB::put_timestamp(const std::string& key, const Timestamp& value) {
    return put_typed(key, TypedValue(value));
}

bool TypedKVDB::put_date(const std::string& key, const Date& value) {
    return put_typed(key, TypedValue(value));
}

bool TypedKVDB::get_timestamp(const std::string& key, Timestamp& value) {
    TypedValue typed_value;
    if (get_typed(key, typed_value) && typed_value.is_timestamp()) {
        value = typed_value.as_timestamp();
        return true;
    }
    return false;
}

bool TypedKVDB::get_date(const std::string& key, Date& value) {
    TypedValue typed_value;
    if (get_typed(key, typed_value) && typed_value.is_date()) {
        value = typed_value.as_date();
        return true;
    }
    return false;
}

// 时间字符串操作
bool TypedKVDB::put_timestamp_str(const std::string& key, const std::string& timestamp_str) {
    try {
        Timestamp ts = parse_timestamp(timestamp_str);
        return put_timestamp(key, ts);
    } catch (const std::exception&) {
        return false;
    }
}

bool TypedKVDB::put_date_str(const std::string& key, const std::string& date_str) {
    try {
        Date date = parse_date(date_str);
        return put_date(key, date);
    } catch (const std::exception&) {
        return false;
    }
}

bool TypedKVDB::get_timestamp_str(const std::string& key, std::string& timestamp_str) {
    Timestamp ts;
    if (get_timestamp(key, ts)) {
        timestamp_str = format_timestamp(ts);
        return true;
    }
    return false;
}

bool TypedKVDB::get_date_str(const std::string& key, std::string& date_str) {
    Date date;
    if (get_date(key, date)) {
        date_str = format_date(date);
        return true;
    }
    return false;
}

// 集合类型操作
bool TypedKVDB::put_list(const std::string& key, const List& value) {
    return put_typed(key, TypedValue(value));
}

bool TypedKVDB::put_set(const std::string& key, const Set& value) {
    return put_typed(key, TypedValue(value));
}

bool TypedKVDB::put_map(const std::string& key, const Map& value) {
    return put_typed(key, TypedValue(value));
}

bool TypedKVDB::get_list(const std::string& key, List& value) {
    TypedValue typed_value;
    if (get_typed(key, typed_value) && typed_value.is_list()) {
        value = typed_value.as_list();
        return true;
    }
    return false;
}

bool TypedKVDB::get_set(const std::string& key, Set& value) {
    TypedValue typed_value;
    if (get_typed(key, typed_value) && typed_value.is_set()) {
        value = typed_value.as_set();
        return true;
    }
    return false;
}

bool TypedKVDB::get_map(const std::string& key, Map& value) {
    TypedValue typed_value;
    if (get_typed(key, typed_value) && typed_value.is_map()) {
        value = typed_value.as_map();
        return true;
    }
    return false;
}

// 二进制类型操作
bool TypedKVDB::put_blob(const std::string& key, const Blob& value) {
    return put_typed(key, TypedValue(value));
}

bool TypedKVDB::get_blob(const std::string& key, Blob& value) {
    TypedValue typed_value;
    if (get_typed(key, typed_value) && typed_value.is_blob()) {
        value = typed_value.as_blob();
        return true;
    }
    return false;
}

bool TypedKVDB::put_blob_from_file(const std::string& key, const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        return false;
    }
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    Blob blob(size);
    file.read(reinterpret_cast<char*>(blob.data()), size);
    
    return put_blob(key, blob);
}

bool TypedKVDB::get_blob_to_file(const std::string& key, const std::string& file_path) {
    Blob blob;
    if (!get_blob(key, blob)) {
        return false;
    }
    
    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(blob.data()), blob.size());
    return true;
}

// 列表操作
bool TypedKVDB::list_append(const std::string& key, const TypedValue& value) {
    uint64_t seq = next_seq();
    return typed_memtable_->list_append(key, value, seq);
}

bool TypedKVDB::list_prepend(const std::string& key, const TypedValue& value) {
    uint64_t seq = next_seq();
    return typed_memtable_->list_prepend(key, value, seq);
}

bool TypedKVDB::list_remove(const std::string& key, size_t index) {
    uint64_t seq = next_seq();
    return typed_memtable_->list_remove(key, index, seq);
}

bool TypedKVDB::list_get(const std::string& key, size_t index, TypedValue& value) {
    Snapshot snapshot = get_snapshot();
    bool result = typed_memtable_->list_get(key, index, snapshot.seq, value);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::list_set(const std::string& key, size_t index, const TypedValue& value) {
    uint64_t seq = next_seq();
    return typed_memtable_->list_set(key, index, value, seq);
}

bool TypedKVDB::list_size(const std::string& key, size_t& size) {
    Snapshot snapshot = get_snapshot();
    bool result = typed_memtable_->list_size(key, snapshot.seq, size);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::list_clear(const std::string& key) {
    return put_list(key, List{});
}

// 集合操作
bool TypedKVDB::set_add(const std::string& key, const TypedValue& value) {
    uint64_t seq = next_seq();
    return typed_memtable_->set_add(key, value, seq);
}

bool TypedKVDB::set_remove(const std::string& key, const TypedValue& value) {
    uint64_t seq = next_seq();
    return typed_memtable_->set_remove(key, value, seq);
}

bool TypedKVDB::set_contains(const std::string& key, const TypedValue& value) {
    Snapshot snapshot = get_snapshot();
    bool result = typed_memtable_->set_contains(key, value, snapshot.seq);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::set_size(const std::string& key, size_t& size) {
    Snapshot snapshot = get_snapshot();
    bool result = typed_memtable_->set_size(key, snapshot.seq, size);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::set_clear(const std::string& key) {
    return put_set(key, Set{});
}

bool TypedKVDB::set_union(const std::string& key1, const std::string& key2, const std::string& result_key) {
    Set set1, set2;
    if (!get_set(key1, set1) || !get_set(key2, set2)) {
        return false;
    }
    
    Set result_set = set1;
    for (const auto& item : set2) {
        result_set.insert(item);
    }
    
    return put_set(result_key, result_set);
}

bool TypedKVDB::set_intersection(const std::string& key1, const std::string& key2, const std::string& result_key) {
    Set set1, set2;
    if (!get_set(key1, set1) || !get_set(key2, set2)) {
        return false;
    }
    
    Set result_set;
    for (const auto& item : set1) {
        if (set2.find(item) != set2.end()) {
            result_set.insert(item);
        }
    }
    
    return put_set(result_key, result_set);
}

bool TypedKVDB::set_difference(const std::string& key1, const std::string& key2, const std::string& result_key) {
    Set set1, set2;
    if (!get_set(key1, set1) || !get_set(key2, set2)) {
        return false;
    }
    
    Set result_set;
    for (const auto& item : set1) {
        if (set2.find(item) == set2.end()) {
            result_set.insert(item);
        }
    }
    
    return put_set(result_key, result_set);
}

// 映射操作
bool TypedKVDB::map_put(const std::string& key, const std::string& field, const TypedValue& value) {
    uint64_t seq = next_seq();
    return typed_memtable_->map_put(key, field, value, seq);
}

bool TypedKVDB::map_get(const std::string& key, const std::string& field, TypedValue& value) {
    Snapshot snapshot = get_snapshot();
    bool result = typed_memtable_->map_get(key, field, snapshot.seq, value);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::map_remove(const std::string& key, const std::string& field) {
    uint64_t seq = next_seq();
    return typed_memtable_->map_remove(key, field, seq);
}

bool TypedKVDB::map_contains(const std::string& key, const std::string& field) {
    Snapshot snapshot = get_snapshot();
    bool result = typed_memtable_->map_contains(key, field, snapshot.seq);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::map_size(const std::string& key, size_t& size) {
    Snapshot snapshot = get_snapshot();
    bool result = typed_memtable_->map_size(key, snapshot.seq, size);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::map_keys(const std::string& key, std::vector<std::string>& keys) {
    Snapshot snapshot = get_snapshot();
    bool result = typed_memtable_->map_keys(key, snapshot.seq, keys);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::map_values(const std::string& key, std::vector<TypedValue>& values) {
    Map map;
    if (get_map(key, map)) {
        values.clear();
        values.reserve(map.size());
        for (const auto& [k, v] : map) {
            values.push_back(v);
        }
        return true;
    }
    return false;
}

bool TypedKVDB::map_clear(const std::string& key) {
    return put_map(key, Map{});
}

// 类型检查和转换
DataType TypedKVDB::get_key_type(const std::string& key) {
    Snapshot snapshot = get_snapshot();
    DataType result = typed_memtable_->get_key_type(key, snapshot.seq);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::key_exists_typed(const std::string& key) {
    Snapshot snapshot = get_snapshot();
    bool result = typed_memtable_->key_exists(key, snapshot.seq);
    release_snapshot(snapshot);
    return result;
}

bool TypedKVDB::convert_key_type(const std::string& key, DataType target_type) {
    TypedValue value;
    if (get_typed(key, value)) {
        try {
            TypedValue converted = value.convert_to(target_type);
            return put_typed(key, converted);
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

// 范围查询
std::vector<std::pair<std::string, TypedValue>> TypedKVDB::range_scan_typed(
    const std::string& start_key, 
    const std::string& end_key,
    size_t limit) {
    
    Snapshot snapshot = get_snapshot();
    auto result = typed_memtable_->range_scan(start_key, end_key, snapshot.seq, limit);
    release_snapshot(snapshot);
    return result;
}

std::vector<std::pair<std::string, TypedValue>> TypedKVDB::type_scan(
    DataType type, 
    size_t limit) {
    
    Snapshot snapshot = get_snapshot();
    auto result = typed_memtable_->type_scan(type, snapshot.seq, limit);
    release_snapshot(snapshot);
    return result;
}

std::vector<std::pair<std::string, TypedValue>> TypedKVDB::numeric_range_scan(
    double min_value, 
    double max_value,
    size_t limit) {
    
    std::vector<std::pair<std::string, TypedValue>> result;
    
    // 扫描数值类型
    auto int_results = type_scan(DataType::INT, 0);
    auto float_results = type_scan(DataType::FLOAT, 0);
    auto double_results = type_scan(DataType::DOUBLE, 0);
    
    // 过滤范围
    for (const auto& [key, value] : int_results) {
        double val = static_cast<double>(value.as_int());
        if (val >= min_value && val <= max_value) {
            result.emplace_back(key, value);
            if (limit > 0 && result.size() >= limit) break;
        }
    }
    
    if (limit == 0 || result.size() < limit) {
        for (const auto& [key, value] : float_results) {
            double val = static_cast<double>(value.as_float());
            if (val >= min_value && val <= max_value) {
                result.emplace_back(key, value);
                if (limit > 0 && result.size() >= limit) break;
            }
        }
    }
    
    if (limit == 0 || result.size() < limit) {
        for (const auto& [key, value] : double_results) {
            double val = value.as_double();
            if (val >= min_value && val <= max_value) {
                result.emplace_back(key, value);
                if (limit > 0 && result.size() >= limit) break;
            }
        }
    }
    
    return result;
}

std::vector<std::pair<std::string, TypedValue>> TypedKVDB::timestamp_range_scan(
    const Timestamp& start_time,
    const Timestamp& end_time,
    size_t limit) {
    
    std::vector<std::pair<std::string, TypedValue>> result;
    auto timestamp_results = type_scan(DataType::TIMESTAMP, 0);
    
    for (const auto& [key, value] : timestamp_results) {
        const auto& ts = value.as_timestamp();
        if (ts >= start_time && ts <= end_time) {
            result.emplace_back(key, value);
            if (limit > 0 && result.size() >= limit) break;
        }
    }
    
    return result;
}

std::vector<std::pair<std::string, TypedValue>> TypedKVDB::date_range_scan(
    const Date& start_date,
    const Date& end_date,
    size_t limit) {
    
    std::vector<std::pair<std::string, TypedValue>> result;
    auto date_results = type_scan(DataType::DATE, 0);
    
    for (const auto& [key, value] : date_results) {
        const auto& date = value.as_date();
        if (!(date < start_date) && !(end_date < date)) {
            result.emplace_back(key, value);
            if (limit > 0 && result.size() >= limit) break;
        }
    }
    
    return result;
}

// 统计信息
TypedKVDB::TypeStats TypedKVDB::get_type_statistics() {
    TypeStats stats;
    
    // 遍历所有类型
    for (int type_int = static_cast<int>(DataType::NULL_TYPE); 
         type_int <= static_cast<int>(DataType::BLOB); 
         ++type_int) {
        
        DataType type = static_cast<DataType>(type_int);
        auto results = type_scan(type, 0);
        
        switch (type) {
            case DataType::NULL_TYPE: stats.null_count = results.size(); break;
            case DataType::INT: stats.int_count = results.size(); break;
            case DataType::FLOAT: stats.float_count = results.size(); break;
            case DataType::DOUBLE: stats.double_count = results.size(); break;
            case DataType::STRING: stats.string_count = results.size(); break;
            case DataType::TIMESTAMP: stats.timestamp_count = results.size(); break;
            case DataType::DATE: stats.date_count = results.size(); break;
            case DataType::LIST: stats.list_count = results.size(); break;
            case DataType::SET: stats.set_count = results.size(); break;
            case DataType::MAP: stats.map_count = results.size(); break;
            case DataType::BLOB: stats.blob_count = results.size(); break;
        }
    }
    
    return stats;
}

// 批量操作
bool TypedKVDB::batch_execute(const std::vector<TypedOperation>& operations) {
    for (const auto& op : operations) {
        switch (op.type) {
            case TypedOperation::PUT:
                if (!put_typed(op.key, op.value)) {
                    return false;
                }
                break;
            case TypedOperation::DELETE:
                if (!del(op.key)) {
                    return false;
                }
                break;
        }
    }
    return true;
}

// 事务实现
TypedKVDB::TypedTransaction::TypedTransaction(TypedKVDB* db) 
    : db_(db), committed_(false), rolled_back_(false) {
}

TypedKVDB::TypedTransaction::~TypedTransaction() {
    if (!committed_ && !rolled_back_) {
        rollback();
    }
}

bool TypedKVDB::TypedTransaction::put_typed(const std::string& key, const TypedValue& value) {
    if (committed_ || rolled_back_) {
        return false;
    }
    operations_.emplace_back(TypedOperation::PUT, key, value);
    return true;
}

bool TypedKVDB::TypedTransaction::get_typed(const std::string& key, TypedValue& value) {
    if (committed_ || rolled_back_) {
        return false;
    }
    return db_->get_typed(key, value);
}

bool TypedKVDB::TypedTransaction::del(const std::string& key) {
    if (committed_ || rolled_back_) {
        return false;
    }
    operations_.emplace_back(TypedOperation::DELETE, key);
    return true;
}

bool TypedKVDB::TypedTransaction::commit() {
    if (committed_ || rolled_back_) {
        return false;
    }
    
    bool success = db_->batch_execute(operations_);
    if (success) {
        committed_ = true;
    }
    return success;
}

void TypedKVDB::TypedTransaction::rollback() {
    if (!committed_ && !rolled_back_) {
        operations_.clear();
        rolled_back_ = true;
    }
}

std::unique_ptr<TypedKVDB::TypedTransaction> TypedKVDB::begin_transaction() {
    return std::make_unique<TypedTransaction>(this);
}

} // namespace kvdb