#include "secondary_index.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <mutex>
#include <shared_mutex>

SecondaryIndex::SecondaryIndex(const std::string& name, const std::string& field, bool unique)
    : name_(name), field_(field), is_unique_(unique), total_entries_(0), stats_dirty_(true) {
}

SecondaryIndex::~SecondaryIndex() = default;

void SecondaryIndex::insert(const std::string& indexed_value, const std::string& primary_key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    if (is_unique_ && !validate_unique_constraint(indexed_value, primary_key)) {
        throw std::runtime_error("Unique constraint violation for value: " + indexed_value);
    }
    
    index_map_[indexed_value].insert(primary_key);
    total_entries_++;
    stats_dirty_ = true;
}

void SecondaryIndex::remove(const std::string& indexed_value, const std::string& primary_key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto it = index_map_.find(indexed_value);
    if (it != index_map_.end()) {
        it->second.erase(primary_key);
        if (it->second.empty()) {
            index_map_.erase(it);
        }
        total_entries_--;
        stats_dirty_ = true;
    }
}

void SecondaryIndex::update(const std::string& old_value, const std::string& new_value, 
                           const std::string& primary_key) {
    if (old_value == new_value) {
        return;
    }
    
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 检查唯一性约束
    if (is_unique_ && !validate_unique_constraint(new_value, primary_key)) {
        throw std::runtime_error("Unique constraint violation for value: " + new_value);
    }
    
    // 从旧值中移除
    auto old_it = index_map_.find(old_value);
    if (old_it != index_map_.end()) {
        old_it->second.erase(primary_key);
        if (old_it->second.empty()) {
            index_map_.erase(old_it);
        }
    }
    
    // 添加到新值
    index_map_[new_value].insert(primary_key);
    stats_dirty_ = true;
}

std::vector<std::string> SecondaryIndex::exact_lookup(const std::string& value) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> result;
    auto it = index_map_.find(value);
    if (it != index_map_.end()) {
        result.assign(it->second.begin(), it->second.end());
    }
    return result;
}

std::vector<std::string> SecondaryIndex::range_lookup(const std::string& start, const std::string& end) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> result;
    auto start_it = index_map_.lower_bound(start);
    auto end_it = index_map_.upper_bound(end);
    
    for (auto it = start_it; it != end_it; ++it) {
        result.insert(result.end(), it->second.begin(), it->second.end());
    }
    
    return result;
}

std::vector<std::string> SecondaryIndex::prefix_lookup(const std::string& prefix) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> result;
    auto start_it = index_map_.lower_bound(prefix);
    
    for (auto it = start_it; it != index_map_.end(); ++it) {
        if (it->first.substr(0, prefix.length()) != prefix) {
            break;
        }
        result.insert(result.end(), it->second.begin(), it->second.end());
    }
    
    return result;
}

size_t SecondaryIndex::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return total_entries_;
}

size_t SecondaryIndex::unique_values() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return index_map_.size();
}

double SecondaryIndex::selectivity() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (total_entries_ == 0) return 0.0;
    return static_cast<double>(index_map_.size()) / total_entries_;
}

size_t SecondaryIndex::memory_usage() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    size_t usage = sizeof(*this);
    for (const auto& pair : index_map_) {
        usage += pair.first.size();
        usage += sizeof(std::set<std::string>);
        for (const auto& key : pair.second) {
            usage += key.size();
        }
    }
    return usage;
}

void SecondaryIndex::serialize(std::ostream& out) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    // 写入基本信息
    size_t name_len = name_.size();
    out.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
    out.write(name_.c_str(), name_len);
    
    size_t field_len = field_.size();
    out.write(reinterpret_cast<const char*>(&field_len), sizeof(field_len));
    out.write(field_.c_str(), field_len);
    
    out.write(reinterpret_cast<const char*>(&is_unique_), sizeof(is_unique_));
    
    // 写入索引数据
    size_t map_size = index_map_.size();
    out.write(reinterpret_cast<const char*>(&map_size), sizeof(map_size));
    
    for (const auto& pair : index_map_) {
        // 写入索引值
        size_t value_len = pair.first.size();
        out.write(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
        out.write(pair.first.c_str(), value_len);
        
        // 写入主键集合
        size_t set_size = pair.second.size();
        out.write(reinterpret_cast<const char*>(&set_size), sizeof(set_size));
        
        for (const auto& key : pair.second) {
            size_t key_len = key.size();
            out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
            out.write(key.c_str(), key_len);
        }
    }
}

void SecondaryIndex::deserialize(std::istream& in) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 读取基本信息
    size_t name_len;
    in.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
    name_.resize(name_len);
    in.read(&name_[0], name_len);
    
    size_t field_len;
    in.read(reinterpret_cast<char*>(&field_len), sizeof(field_len));
    field_.resize(field_len);
    in.read(&field_[0], field_len);
    
    in.read(reinterpret_cast<char*>(&is_unique_), sizeof(is_unique_));
    
    // 读取索引数据
    index_map_.clear();
    size_t map_size;
    in.read(reinterpret_cast<char*>(&map_size), sizeof(map_size));
    
    for (size_t i = 0; i < map_size; ++i) {
        // 读取索引值
        size_t value_len;
        in.read(reinterpret_cast<char*>(&value_len), sizeof(value_len));
        std::string value(value_len, '\0');
        in.read(&value[0], value_len);
        
        // 读取主键集合
        size_t set_size;
        in.read(reinterpret_cast<char*>(&set_size), sizeof(set_size));
        
        std::set<std::string> keys;
        for (size_t j = 0; j < set_size; ++j) {
            size_t key_len;
            in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            std::string key(key_len, '\0');
            in.read(&key[0], key_len);
            keys.insert(key);
        }
        
        index_map_[value] = std::move(keys);
    }
    
    update_stats();
}

void SecondaryIndex::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    index_map_.clear();
    total_entries_ = 0;
    stats_dirty_ = true;
}

std::vector<std::string> SecondaryIndex::get_all_values() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> values;
    values.reserve(index_map_.size());
    
    for (const auto& pair : index_map_) {
        values.push_back(pair.first);
    }
    
    return values;
}

void SecondaryIndex::update_stats() const {
    if (!stats_dirty_) return;
    
    total_entries_ = 0;
    for (const auto& pair : index_map_) {
        total_entries_ += pair.second.size();
    }
    stats_dirty_ = false;
}

bool SecondaryIndex::validate_unique_constraint(const std::string& value, const std::string& key) const {
    if (!is_unique_) return true;
    
    auto it = index_map_.find(value);
    if (it == index_map_.end()) return true;
    
    // 如果已存在该值，检查是否是同一个key的更新
    return it->second.size() == 1 && it->second.count(key) > 0;
}