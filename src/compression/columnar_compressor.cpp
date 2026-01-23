#include "columnar_compressor.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <regex>

ColumnarCompressor::ColumnarCompressor() {
    std::cout << "[ColumnarCompressor] 初始化列式压缩器（相同key模式的value压缩）\n";
}

std::optional<std::string> ColumnarCompressor::compress(const std::string& data) {
    // 单个数据项的压缩，效果有限
    // 列式压缩更适合批量数据
    return data;
}

std::optional<std::string> ColumnarCompressor::decompress(const std::string& compressed_data) {
    return compressed_data;
}

std::optional<std::string> ColumnarCompressor::compress_batch(
    const std::vector<std::pair<std::string, std::string>>& kv_pairs) {
    
    if (kv_pairs.empty()) return "";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        // 1. 按key模式分组
        auto groups = group_by_key_pattern(kv_pairs);
        
        // 2. 压缩每个分组的value列
        for (auto& group : groups) {
            group.compressed_values = compress_value_column(group.values);
        }
        
        // 3. 序列化压缩结果
        std::string result = serialize_column_groups(groups);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        // 计算原始大小
        uint64_t original_size = 0;
        for (const auto& [key, value] : kv_pairs) {
            original_size += key.size() + value.size();
        }
        
        update_compression_stats(original_size, result.size(), duration);
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "[ColumnarCompressor] 批量压缩失败: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::vector<std::pair<std::string, std::string>> ColumnarCompressor::decompress_batch(
    const std::string& compressed_data) {
    
    if (compressed_data.empty()) return {};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        // 1. 反序列化列分组
        auto groups = deserialize_column_groups(compressed_data);
        
        // 2. 解压每个分组的value列
        std::vector<std::pair<std::string, std::string>> result;
        
        for (const auto& group : groups) {
            auto values = decompress_value_column(group.compressed_values);
            
            // 重建key-value对
            for (size_t i = 0; i < values.size(); ++i) {
                std::string key = group.key_pattern + "_" + std::to_string(i);
                result.emplace_back(key, values[i]);
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        update_decompression_stats(duration);
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "[ColumnarCompressor] 批量解压失败: " << e.what() << "\n";
        return {};
    }
}

std::vector<ColumnarCompressor::ColumnGroup> ColumnarCompressor::group_by_key_pattern(
    const std::vector<std::pair<std::string, std::string>>& kv_pairs) {
    
    std::unordered_map<std::string, std::vector<std::string>> pattern_groups;
    
    // 按key模式分组
    for (const auto& [key, value] : kv_pairs) {
        std::string pattern = extract_key_pattern(key);
        pattern_groups[pattern].push_back(value);
    }
    
    std::vector<ColumnGroup> groups;
    for (const auto& [pattern, values] : pattern_groups) {
        if (values.size() >= min_group_size_) {
            ColumnGroup group;
            group.key_pattern = pattern;
            group.values = values;
            groups.push_back(std::move(group));
        }
    }
    
    std::cout << "[ColumnarCompressor] 分组结果: " << groups.size() << " 个列分组\n";
    return groups;
}

std::string ColumnarCompressor::extract_key_pattern(const std::string& key) {
    // 提取key的模式，例如：
    // "user_123" -> "user_*"
    // "product_456_info" -> "product_*_info"
    
    std::string pattern = key;
    
    // 使用正则表达式替换数字为通配符
    std::regex number_regex(R"(\d+)");
    pattern = std::regex_replace(pattern, number_regex, "*");
    
    return pattern;
}

std::string ColumnarCompressor::compress_value_column(const std::vector<std::string>& values) {
    if (values.empty()) return "";
    
    // 列式压缩策略：
    // 1. 字典压缩：相同value用索引替代
    // 2. 前缀压缩：相同前缀只存储一次
    // 3. 差分压缩：数值类型的差分编码
    
    std::unordered_map<std::string, uint16_t> dictionary;
    std::vector<uint16_t> indices;
    std::vector<std::string> unique_values;
    
    // 构建字典
    for (const auto& value : values) {
        if (dictionary.find(value) == dictionary.end()) {
            dictionary[value] = static_cast<uint16_t>(unique_values.size());
            unique_values.push_back(value);
        }
        indices.push_back(dictionary[value]);
    }
    
    // 序列化压缩结果
    std::ostringstream oss;
    
    // 写入字典大小
    oss << unique_values.size() << "|";
    
    // 写入字典
    for (const auto& value : unique_values) {
        oss << value.size() << ":" << value << "|";
    }
    
    // 写入索引序列
    oss << indices.size() << "|";
    for (uint16_t index : indices) {
        oss << index << ",";
    }
    
    std::string result = oss.str();
    
    // 计算压缩比
    size_t original_size = 0;
    for (const auto& value : values) {
        original_size += value.size();
    }
    
    double compression_ratio = (double)result.size() / original_size;
    std::cout << "[ColumnarCompressor] 列压缩比: " << compression_ratio 
              << " (" << original_size << " -> " << result.size() << " bytes)\n";
    
    return result;
}

std::vector<std::string> ColumnarCompressor::decompress_value_column(const std::string& compressed_values) {
    if (compressed_values.empty()) return {};
    
    std::istringstream iss(compressed_values);
    std::string token;
    
    // 读取字典大小
    std::getline(iss, token, '|');
    size_t dict_size = std::stoul(token);
    
    // 读取字典
    std::vector<std::string> dictionary(dict_size);
    for (size_t i = 0; i < dict_size; ++i) {
        std::getline(iss, token, '|');
        size_t colon_pos = token.find(':');
        if (colon_pos != std::string::npos) {
            size_t value_size = std::stoul(token.substr(0, colon_pos));
            dictionary[i] = token.substr(colon_pos + 1, value_size);
        }
    }
    
    // 读取索引序列
    std::getline(iss, token, '|');
    size_t indices_count = std::stoul(token);
    
    std::vector<std::string> result;
    result.reserve(indices_count);
    
    std::string indices_str;
    std::getline(iss, indices_str);
    std::istringstream indices_iss(indices_str);
    
    std::string index_str;
    while (std::getline(indices_iss, index_str, ',') && !index_str.empty()) {
        uint16_t index = static_cast<uint16_t>(std::stoul(index_str));
        if (index < dictionary.size()) {
            result.push_back(dictionary[index]);
        }
    }
    
    return result;
}

std::string ColumnarCompressor::serialize_column_groups(const std::vector<ColumnGroup>& groups) {
    std::ostringstream oss;
    
    // 写入分组数量
    oss << groups.size() << "\n";
    
    // 写入每个分组
    for (const auto& group : groups) {
        oss << group.key_pattern << "\n";
        oss << group.compressed_values.size() << "\n";
        oss << group.compressed_values << "\n";
    }
    
    return oss.str();
}

std::vector<ColumnarCompressor::ColumnGroup> ColumnarCompressor::deserialize_column_groups(const std::string& data) {
    std::istringstream iss(data);
    std::string line;
    
    // 读取分组数量
    std::getline(iss, line);
    size_t group_count = std::stoul(line);
    
    std::vector<ColumnGroup> groups;
    groups.reserve(group_count);
    
    // 读取每个分组
    for (size_t i = 0; i < group_count; ++i) {
        ColumnGroup group;
        
        // 读取key模式
        std::getline(iss, group.key_pattern);
        
        // 读取压缩数据大小
        std::getline(iss, line);
        size_t data_size = std::stoul(line);
        
        // 读取压缩数据
        group.compressed_values.resize(data_size);
        iss.read(&group.compressed_values[0], data_size);
        iss.ignore(); // 跳过换行符
        
        groups.push_back(std::move(group));
    }
    
    return groups;
}

void ColumnarCompressor::update_compression_stats(uint64_t original_size, uint64_t compressed_size, uint64_t time_us) {
    stats_.original_size += original_size;
    stats_.compressed_size += compressed_size;
    stats_.compression_time_us += time_us;
    stats_.compression_count++;
}

void ColumnarCompressor::update_decompression_stats(uint64_t time_us) {
    stats_.decompression_time_us += time_us;
    stats_.decompression_count++;
}

void ColumnarCompressor::reset_stats() {
    stats_ = CompressionStats{};
}