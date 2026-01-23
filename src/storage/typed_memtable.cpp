#include "typed_memtable.h"
#include <algorithm>
#include <sstream>

namespace kvdb {

// TypedVersionedValue 实现
std::string TypedVersionedValue::serialize() const {
    std::ostringstream oss;
    
    // 写入序列号
    oss.write(reinterpret_cast<const char*>(&seq), sizeof(seq));
    
    // 写入删除标志
    oss.write(reinterpret_cast<const char*>(&is_deleted), sizeof(is_deleted));
    
    // 写入值
    if (!is_deleted) {
        std::string value_data = value.serialize();
        uint32_t value_len = value_data.length();
        oss.write(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
        oss.write(value_data.data(), value_len);
    }
    
    return oss.str();
}

TypedVersionedValue TypedVersionedValue::deserialize(const std::string& data) {
    std::istringstream iss(data);
    
    TypedVersionedValue result;
    
    // 读取序列号
    iss.read(reinterpret_cast<char*>(&result.seq), sizeof(result.seq));
    
    // 读取删除标志
    iss.read(reinterpret_cast<char*>(&result.is_deleted), sizeof(result.is_deleted));
    
    // 读取值
    if (!result.is_deleted) {
        uint32_t value_len;
        iss.read(reinterpret_cast<char*>(&value_len), sizeof(value_len));
        std::string value_data(value_len, '\0');
        iss.read(&value_data[0], value_len);
        result.value = TypedValue::deserialize(value_data);
    }
    
    return result;
}

// TypedMemTable 实现
void TypedMemTable::put(const std::string& key, const TypedValue& value, uint64_t seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    TypedVersionedValue version(seq, value, false);
    add_version(key, version);
    
    size_bytes_ += key.size() + value.size() + sizeof(TypedVersionedValue);
}

bool TypedMemTable::get(const std::string& key, uint64_t snapshot_seq, TypedValue& value) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    const TypedValue* latest = get_latest_value(key, snapshot_seq);
    if (latest) {
        value = *latest;
        return true;
    }
    return false;
}

void TypedMemTable::del(const std::string& key, uint64_t seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    TypedVersionedValue version(seq, TypedValue(), true);
    add_version(key, version);
    
    size_bytes_ += key.size() + sizeof(TypedVersionedValue);
}

// 类型化操作实现
void TypedMemTable::put_int(const std::string& key, int64_t value, uint64_t seq) {
    put(key, TypedValue(value), seq);
}

void TypedMemTable::put_float(const std::string& key, float value, uint64_t seq) {
    put(key, TypedValue(value), seq);
}

void TypedMemTable::put_double(const std::string& key, double value, uint64_t seq) {
    put(key, TypedValue(value), seq);
}

void TypedMemTable::put_string(const std::string& key, const std::string& value, uint64_t seq) {
    put(key, TypedValue(value), seq);
}

void TypedMemTable::put_timestamp(const std::string& key, const Timestamp& value, uint64_t seq) {
    put(key, TypedValue(value), seq);
}

void TypedMemTable::put_date(const std::string& key, const Date& value, uint64_t seq) {
    put(key, TypedValue(value), seq);
}

void TypedMemTable::put_list(const std::string& key, const List& value, uint64_t seq) {
    put(key, TypedValue(value), seq);
}

void TypedMemTable::put_set(const std::string& key, const Set& value, uint64_t seq) {
    put(key, TypedValue(value), seq);
}

void TypedMemTable::put_map(const std::string& key, const Map& value, uint64_t seq) {
    put(key, TypedValue(value), seq);
}

void TypedMemTable::put_blob(const std::string& key, const Blob& value, uint64_t seq) {
    put(key, TypedValue(value), seq);
}

// 类型化获取实现
bool TypedMemTable::get_int(const std::string& key, uint64_t snapshot_seq, int64_t& value) const {
    TypedValue typed_value;
    if (get(key, snapshot_seq, typed_value) && typed_value.is_int()) {
        value = typed_value.as_int();
        return true;
    }
    return false;
}

bool TypedMemTable::get_float(const std::string& key, uint64_t snapshot_seq, float& value) const {
    TypedValue typed_value;
    if (get(key, snapshot_seq, typed_value) && typed_value.is_float()) {
        value = typed_value.as_float();
        return true;
    }
    return false;
}

bool TypedMemTable::get_double(const std::string& key, uint64_t snapshot_seq, double& value) const {
    TypedValue typed_value;
    if (get(key, snapshot_seq, typed_value) && typed_value.is_double()) {
        value = typed_value.as_double();
        return true;
    }
    return false;
}

bool TypedMemTable::get_string(const std::string& key, uint64_t snapshot_seq, std::string& value) const {
    TypedValue typed_value;
    if (get(key, snapshot_seq, typed_value) && typed_value.is_string()) {
        value = typed_value.as_string();
        return true;
    }
    return false;
}

bool TypedMemTable::get_timestamp(const std::string& key, uint64_t snapshot_seq, Timestamp& value) const {
    TypedValue typed_value;
    if (get(key, snapshot_seq, typed_value) && typed_value.is_timestamp()) {
        value = typed_value.as_timestamp();
        return true;
    }
    return false;
}

bool TypedMemTable::get_date(const std::string& key, uint64_t snapshot_seq, Date& value) const {
    TypedValue typed_value;
    if (get(key, snapshot_seq, typed_value) && typed_value.is_date()) {
        value = typed_value.as_date();
        return true;
    }
    return false;
}

bool TypedMemTable::get_list(const std::string& key, uint64_t snapshot_seq, List& value) const {
    TypedValue typed_value;
    if (get(key, snapshot_seq, typed_value) && typed_value.is_list()) {
        value = typed_value.as_list();
        return true;
    }
    return false;
}

bool TypedMemTable::get_set(const std::string& key, uint64_t snapshot_seq, Set& value) const {
    TypedValue typed_value;
    if (get(key, snapshot_seq, typed_value) && typed_value.is_set()) {
        value = typed_value.as_set();
        return true;
    }
    return false;
}

bool TypedMemTable::get_map(const std::string& key, uint64_t snapshot_seq, Map& value) const {
    TypedValue typed_value;
    if (get(key, snapshot_seq, typed_value) && typed_value.is_map()) {
        value = typed_value.as_map();
        return true;
    }
    return false;
}

bool TypedMemTable::get_blob(const std::string& key, uint64_t snapshot_seq, Blob& value) const {
    TypedValue typed_value;
    if (get(key, snapshot_seq, typed_value) && typed_value.is_blob()) {
        value = typed_value.as_blob();
        return true;
    }
    return false;
}

// 集合操作实现
bool TypedMemTable::list_append(const std::string& key, const TypedValue& value, uint64_t seq) {
    TypedValue* list_value = get_mutable_value(key, seq);
    if (list_value && list_value->is_list()) {
        list_value->mutable_list().push_back(value);
        return true;
    }
    return false;
}

bool TypedMemTable::list_prepend(const std::string& key, const TypedValue& value, uint64_t seq) {
    TypedValue* list_value = get_mutable_value(key, seq);
    if (list_value && list_value->is_list()) {
        list_value->mutable_list().insert(list_value->mutable_list().begin(), value);
        return true;
    }
    return false;
}

bool TypedMemTable::list_remove(const std::string& key, size_t index, uint64_t seq) {
    TypedValue* list_value = get_mutable_value(key, seq);
    if (list_value && list_value->is_list()) {
        auto& list = list_value->mutable_list();
        if (index < list.size()) {
            list.erase(list.begin() + index);
            return true;
        }
    }
    return false;
}

bool TypedMemTable::list_get(const std::string& key, size_t index, uint64_t snapshot_seq, TypedValue& value) const {
    TypedValue list_value;
    if (get(key, snapshot_seq, list_value) && list_value.is_list()) {
        const auto& list = list_value.as_list();
        if (index < list.size()) {
            value = list[index];
            return true;
        }
    }
    return false;
}

bool TypedMemTable::list_set(const std::string& key, size_t index, const TypedValue& value, uint64_t seq) {
    TypedValue* list_value = get_mutable_value(key, seq);
    if (list_value && list_value->is_list()) {
        auto& list = list_value->mutable_list();
        if (index < list.size()) {
            list[index] = value;
            return true;
        }
    }
    return false;
}

bool TypedMemTable::list_size(const std::string& key, uint64_t snapshot_seq, size_t& size) const {
    TypedValue list_value;
    if (get(key, snapshot_seq, list_value) && list_value.is_list()) {
        size = list_value.as_list().size();
        return true;
    }
    return false;
}

bool TypedMemTable::set_add(const std::string& key, const TypedValue& value, uint64_t seq) {
    TypedValue* set_value = get_mutable_value(key, seq);
    if (set_value && set_value->is_set()) {
        set_value->mutable_set().insert(value);
        return true;
    }
    return false;
}

bool TypedMemTable::set_remove(const std::string& key, const TypedValue& value, uint64_t seq) {
    TypedValue* set_value = get_mutable_value(key, seq);
    if (set_value && set_value->is_set()) {
        set_value->mutable_set().erase(value);
        return true;
    }
    return false;
}

bool TypedMemTable::set_contains(const std::string& key, const TypedValue& value, uint64_t snapshot_seq) const {
    TypedValue set_value;
    if (get(key, snapshot_seq, set_value) && set_value.is_set()) {
        const auto& set = set_value.as_set();
        return set.find(value) != set.end();
    }
    return false;
}

bool TypedMemTable::set_size(const std::string& key, uint64_t snapshot_seq, size_t& size) const {
    TypedValue set_value;
    if (get(key, snapshot_seq, set_value) && set_value.is_set()) {
        size = set_value.as_set().size();
        return true;
    }
    return false;
}

bool TypedMemTable::map_put(const std::string& key, const std::string& field, const TypedValue& value, uint64_t seq) {
    TypedValue* map_value = get_mutable_value(key, seq);
    if (map_value && map_value->is_map()) {
        map_value->mutable_map()[field] = value;
        return true;
    }
    return false;
}

bool TypedMemTable::map_get(const std::string& key, const std::string& field, uint64_t snapshot_seq, TypedValue& value) const {
    TypedValue map_value;
    if (get(key, snapshot_seq, map_value) && map_value.is_map()) {
        const auto& map = map_value.as_map();
        auto it = map.find(field);
        if (it != map.end()) {
            value = it->second;
            return true;
        }
    }
    return false;
}

bool TypedMemTable::map_remove(const std::string& key, const std::string& field, uint64_t seq) {
    TypedValue* map_value = get_mutable_value(key, seq);
    if (map_value && map_value->is_map()) {
        map_value->mutable_map().erase(field);
        return true;
    }
    return false;
}

bool TypedMemTable::map_contains(const std::string& key, const std::string& field, uint64_t snapshot_seq) const {
    TypedValue map_value;
    if (get(key, snapshot_seq, map_value) && map_value.is_map()) {
        const auto& map = map_value.as_map();
        return map.find(field) != map.end();
    }
    return false;
}

bool TypedMemTable::map_size(const std::string& key, uint64_t snapshot_seq, size_t& size) const {
    TypedValue map_value;
    if (get(key, snapshot_seq, map_value) && map_value.is_map()) {
        size = map_value.as_map().size();
        return true;
    }
    return false;
}

bool TypedMemTable::map_keys(const std::string& key, uint64_t snapshot_seq, std::vector<std::string>& keys) const {
    TypedValue map_value;
    if (get(key, snapshot_seq, map_value) && map_value.is_map()) {
        const auto& map = map_value.as_map();
        keys.clear();
        keys.reserve(map.size());
        for (const auto& [k, v] : map) {
            keys.push_back(k);
        }
        return true;
    }
    return false;
}

// 元数据操作
size_t TypedMemTable::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_bytes_;
}

size_t TypedMemTable::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return table_.size();
}

void TypedMemTable::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    table_.clear();
    size_bytes_ = 0;
}

std::map<std::string, std::vector<TypedVersionedValue>> TypedMemTable::get_all_versions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return table_;
}

DataType TypedMemTable::get_key_type(const std::string& key, uint64_t snapshot_seq) const {
    TypedValue value;
    if (get(key, snapshot_seq, value)) {
        return value.get_type();
    }
    return DataType::NULL_TYPE;
}

bool TypedMemTable::key_exists(const std::string& key, uint64_t snapshot_seq) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return get_latest_value(key, snapshot_seq) != nullptr;
}

std::vector<std::pair<std::string, TypedValue>> TypedMemTable::range_scan(
    const std::string& start_key, 
    const std::string& end_key, 
    uint64_t snapshot_seq,
    size_t limit) const {
    
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, TypedValue>> result;
    
    auto start_it = table_.lower_bound(start_key);
    auto end_it = end_key.empty() ? table_.end() : table_.upper_bound(end_key);
    
    for (auto it = start_it; it != end_it && (limit == 0 || result.size() < limit); ++it) {
        const TypedValue* value = get_latest_value(it->first, snapshot_seq);
        if (value) {
            result.emplace_back(it->first, *value);
        }
    }
    
    return result;
}

std::vector<std::pair<std::string, TypedValue>> TypedMemTable::type_scan(
    DataType type, 
    uint64_t snapshot_seq,
    size_t limit) const {
    
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, TypedValue>> result;
    
    for (const auto& [key, versions] : table_) {
        if (limit > 0 && result.size() >= limit) {
            break;
        }
        
        const TypedValue* value = get_latest_value(key, snapshot_seq);
        if (value && value->get_type() == type) {
            result.emplace_back(key, *value);
        }
    }
    
    return result;
}

// 私有辅助方法
TypedValue* TypedMemTable::get_mutable_value(const std::string& key, uint64_t seq) {
    auto it = table_.find(key);
    if (it != table_.end() && !it->second.empty()) {
        // 获取最新版本
        auto& latest = it->second.back();
        if (!latest.is_deleted) {
            return &latest.value;
        }
    }
    return nullptr;
}

const TypedValue* TypedMemTable::get_latest_value(const std::string& key, uint64_t snapshot_seq) const {
    auto it = table_.find(key);
    if (it == table_.end()) {
        return nullptr;
    }
    
    // 从后往前查找符合快照序列号的版本
    for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
        if (rit->seq <= snapshot_seq) {
            if (rit->is_deleted) {
                return nullptr;
            }
            return &rit->value;
        }
    }
    
    return nullptr;
}

void TypedMemTable::update_size(const TypedValue& old_value, const TypedValue& new_value) {
    size_bytes_ = size_bytes_ - old_value.size() + new_value.size();
}

void TypedMemTable::add_version(const std::string& key, const TypedVersionedValue& version) {
    auto& versions = table_[key];
    
    // 保持版本按序列号排序
    auto insert_pos = std::upper_bound(versions.begin(), versions.end(), version,
        [](const TypedVersionedValue& a, const TypedVersionedValue& b) {
            return a.seq < b.seq;
        });
    
    versions.insert(insert_pos, version);
}

} // namespace kvdb