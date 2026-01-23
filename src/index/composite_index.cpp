#include "composite_index.h"
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <mutex>
#include <shared_mutex>

CompositeIndex::CompositeIndex(const std::string& name, const std::vector<std::string>& fields)
    : name_(name), fields_(fields), total_entries_(0), stats_dirty_(true) {
    if (fields_.empty()) {
        throw std::invalid_argument("Composite index must have at least one field");
    }
}

CompositeIndex::~CompositeIndex() = default;

void CompositeIndex::insert(const std::vector<std::string>& indexed_values, const std::string& primary_key) {
    if (!validate_field_count(indexed_values)) {
        throw std::invalid_argument("Field count mismatch");
    }
    
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    std::string composite_key = create_composite_key(indexed_values);
    index_map_[composite_key].insert(primary_key);
    total_entries_++;
    stats_dirty_ = true;
}

void CompositeIndex::remove(const std::vector<std::string>& indexed_values, const std::string& primary_key) {
    if (!validate_field_count(indexed_values)) {
        return;
    }
    
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    std::string composite_key = create_composite_key(indexed_values);
    auto it = index_map_.find(composite_key);
    if (it != index_map_.end()) {
        it->second.erase(primary_key);
        if (it->second.empty()) {
            index_map_.erase(it);
        }
        total_entries_--;
        stats_dirty_ = true;
    }
}

void CompositeIndex::update(const std::vector<std::string>& old_values, 
                           const std::vector<std::string>& new_values, 
                           const std::string& primary_key) {
    if (!validate_field_count(old_values) || !validate_field_count(new_values)) {
        throw std::invalid_argument("Field count mismatch");
    }
    
    if (old_values == new_values) {
        return;
    }
    
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 从旧组合键中移除
    std::string old_composite_key = create_composite_key(old_values);
    auto old_it = index_map_.find(old_composite_key);
    if (old_it != index_map_.end()) {
        old_it->second.erase(primary_key);
        if (old_it->second.empty()) {
            index_map_.erase(old_it);
        }
    }
    
    // 添加到新组合键
    std::string new_composite_key = create_composite_key(new_values);
    index_map_[new_composite_key].insert(primary_key);
    stats_dirty_ = true;
}

std::vector<std::string> CompositeIndex::exact_lookup(const std::vector<std::string>& values) {
    if (!validate_field_count(values)) {
        return {};
    }
    
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::string composite_key = create_composite_key(values);
    auto it = index_map_.find(composite_key);
    if (it != index_map_.end()) {
        return std::vector<std::string>(it->second.begin(), it->second.end());
    }
    return {};
}

std::vector<std::string> CompositeIndex::prefix_lookup(const std::vector<std::string>& prefix_values) {
    if (prefix_values.empty() || prefix_values.size() > fields_.size()) {
        return {};
    }
    
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::string prefix = create_composite_key(prefix_values);
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

std::vector<std::string> CompositeIndex::range_lookup(const std::vector<std::string>& start_values,
                                                     const std::vector<std::string>& end_values) {
    if (!validate_field_count(start_values) || !validate_field_count(end_values)) {
        return {};
    }
    
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::string start_key = create_composite_key(start_values);
    std::string end_key = create_composite_key(end_values);
    
    std::vector<std::string> result;
    auto start_it = index_map_.lower_bound(start_key);
    auto end_it = index_map_.upper_bound(end_key);
    
    for (auto it = start_it; it != end_it; ++it) {
        result.insert(result.end(), it->second.begin(), it->second.end());
    }
    
    return result;
}

std::vector<std::string> CompositeIndex::partial_lookup(const std::vector<std::string>& partial_values) {
    if (partial_values.empty() || partial_values.size() > fields_.size()) {
        return {};
    }
    
    // 对于部分匹配，我们使用前缀查找
    return prefix_lookup(partial_values);
}

size_t CompositeIndex::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return total_entries_;
}

size_t CompositeIndex::unique_combinations() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return index_map_.size();
}

double CompositeIndex::selectivity() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (total_entries_ == 0) return 0.0;
    return static_cast<double>(index_map_.size()) / total_entries_;
}

size_t CompositeIndex::memory_usage() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    size_t usage = sizeof(*this);
    usage += fields_.size() * sizeof(std::string);
    for (const auto& field : fields_) {
        usage += field.size();
    }
    
    for (const auto& pair : index_map_) {
        usage += pair.first.size();
        usage += sizeof(std::set<std::string>);
        for (const auto& key : pair.second) {
            usage += key.size();
        }
    }
    return usage;
}

void CompositeIndex::serialize(std::ostream& out) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    // 写入基本信息
    size_t name_len = name_.size();
    out.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
    out.write(name_.c_str(), name_len);
    
    // 写入字段信息
    size_t field_count = fields_.size();
    out.write(reinterpret_cast<const char*>(&field_count), sizeof(field_count));
    
    for (const auto& field : fields_) {
        size_t field_len = field.size();
        out.write(reinterpret_cast<const char*>(&field_len), sizeof(field_len));
        out.write(field.c_str(), field_len);
    }
    
    // 写入索引数据
    size_t map_size = index_map_.size();
    out.write(reinterpret_cast<const char*>(&map_size), sizeof(map_size));
    
    for (const auto& pair : index_map_) {
        // 写入复合键
        size_t key_len = pair.first.size();
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(pair.first.c_str(), key_len);
        
        // 写入主键集合
        size_t set_size = pair.second.size();
        out.write(reinterpret_cast<const char*>(&set_size), sizeof(set_size));
        
        for (const auto& primary_key : pair.second) {
            size_t pk_len = primary_key.size();
            out.write(reinterpret_cast<const char*>(&pk_len), sizeof(pk_len));
            out.write(primary_key.c_str(), pk_len);
        }
    }
}

void CompositeIndex::deserialize(std::istream& in) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 读取基本信息
    size_t name_len;
    in.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
    name_.resize(name_len);
    in.read(&name_[0], name_len);
    
    // 读取字段信息
    size_t field_count;
    in.read(reinterpret_cast<char*>(&field_count), sizeof(field_count));
    
    fields_.clear();
    fields_.reserve(field_count);
    
    for (size_t i = 0; i < field_count; ++i) {
        size_t field_len;
        in.read(reinterpret_cast<char*>(&field_len), sizeof(field_len));
        std::string field(field_len, '\0');
        in.read(&field[0], field_len);
        fields_.push_back(field);
    }
    
    // 读取索引数据
    index_map_.clear();
    size_t map_size;
    in.read(reinterpret_cast<char*>(&map_size), sizeof(map_size));
    
    for (size_t i = 0; i < map_size; ++i) {
        // 读取复合键
        size_t key_len;
        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        std::string composite_key(key_len, '\0');
        in.read(&composite_key[0], key_len);
        
        // 读取主键集合
        size_t set_size;
        in.read(reinterpret_cast<char*>(&set_size), sizeof(set_size));
        
        std::set<std::string> primary_keys;
        for (size_t j = 0; j < set_size; ++j) {
            size_t pk_len;
            in.read(reinterpret_cast<char*>(&pk_len), sizeof(pk_len));
            std::string primary_key(pk_len, '\0');
            in.read(&primary_key[0], pk_len);
            primary_keys.insert(primary_key);
        }
        
        index_map_[composite_key] = std::move(primary_keys);
    }
    
    update_stats();
}

void CompositeIndex::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    index_map_.clear();
    total_entries_ = 0;
    stats_dirty_ = true;
}

std::string CompositeIndex::create_composite_key(const std::vector<std::string>& values) const {
    if (values.empty()) return "";
    
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            oss << FIELD_SEPARATOR;
        }
        oss << values[i];
    }
    return oss.str();
}

std::vector<std::string> CompositeIndex::parse_composite_key(const std::string& composite_key) const {
    std::vector<std::string> values;
    std::istringstream iss(composite_key);
    std::string value;
    
    while (std::getline(iss, value, FIELD_SEPARATOR)) {
        values.push_back(value);
    }
    
    return values;
}

void CompositeIndex::update_stats() const {
    if (!stats_dirty_) return;
    
    total_entries_ = 0;
    for (const auto& pair : index_map_) {
        total_entries_ += pair.second.size();
    }
    stats_dirty_ = false;
}

bool CompositeIndex::validate_field_count(const std::vector<std::string>& values) const {
    return values.size() == fields_.size();
}