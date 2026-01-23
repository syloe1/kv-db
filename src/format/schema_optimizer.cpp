#include "schema_optimizer.h"
#include <iostream>
#include <algorithm>
#include <regex>
#include <climits>

// Schema 实现
void Schema::add_field(const FieldDescriptor& field) {
    fields_.push_back(field);
    field_name_to_index_[field.name] = fields_.size() - 1;
    field_number_to_index_[field.field_number] = fields_.size() - 1;
}

void Schema::add_field(const std::string& name, DataType type, uint32_t field_number, 
                      bool repeated, bool optional) {
    add_field(FieldDescriptor(name, type, field_number, repeated, optional));
}

const FieldDescriptor* Schema::get_field(uint32_t field_number) const {
    auto it = field_number_to_index_.find(field_number);
    return (it != field_number_to_index_.end()) ? &fields_[it->second] : nullptr;
}

const FieldDescriptor* Schema::get_field(const std::string& name) const {
    auto it = field_name_to_index_.find(name);
    return (it != field_name_to_index_.end()) ? &fields_[it->second] : nullptr;
}

void Schema::optimize_field_order() {
    // 按字段编号排序，确保编码效率
    std::sort(fields_.begin(), fields_.end(), 
              [](const FieldDescriptor& a, const FieldDescriptor& b) {
                  return a.field_number < b.field_number;
              });
    
    // 重建索引
    field_name_to_index_.clear();
    field_number_to_index_.clear();
    
    for (size_t i = 0; i < fields_.size(); ++i) {
        field_name_to_index_[fields_[i].name] = i;
        field_number_to_index_[fields_[i].field_number] = i;
    }
}

void Schema::suggest_data_types(const std::vector<std::unordered_map<std::string, std::string>>& sample_data) {
    auto suggested_types = DataTypeInferrer::infer_schema_from_samples(sample_data);
    
    std::cout << "[Schema] 数据类型建议:\n";
    for (auto& field : fields_) {
        auto it = suggested_types.find(field.name);
        if (it != suggested_types.end() && it->second != field.type) {
            std::cout << "  字段 '" << field.name << "': " 
                      << static_cast<int>(field.type) << " -> " 
                      << static_cast<int>(it->second) << " (建议)\n";
        }
    }
}

// DataTypeInferrer 实现
DataType DataTypeInferrer::infer_type(const std::string& value) {
    if (value.empty()) return DataType::STRING;
    
    if (is_boolean(value)) return DataType::BOOL;
    if (is_integer(value)) {
        int64_t int_val = std::stoll(value);
        return infer_optimal_int_type(int_val);
    }
    if (is_float(value)) return DataType::DOUBLE;
    
    return DataType::STRING;
}

DataType DataTypeInferrer::infer_optimal_int_type(int64_t value) {
    if (value >= 0) {
        // 无符号整数
        if (value <= UINT32_MAX) {
            if (value < 128) return DataType::VARINT; // 小整数用varint更高效
            return DataType::UINT32;
        }
        return DataType::UINT64;
    } else {
        // 有符号整数
        if (value >= INT32_MIN && value <= INT32_MAX) {
            return DataType::INT32;
        }
        return DataType::INT64;
    }
}

bool DataTypeInferrer::is_numeric(const std::string& value) {
    return is_integer(value) || is_float(value);
}

bool DataTypeInferrer::is_integer(const std::string& value) {
    if (value.empty()) return false;
    
    std::regex int_regex(R"(^[+-]?\d+$)");
    return std::regex_match(value, int_regex);
}

bool DataTypeInferrer::is_float(const std::string& value) {
    if (value.empty()) return false;
    
    std::regex float_regex(R"(^[+-]?\d*\.?\d+([eE][+-]?\d+)?$)");
    return std::regex_match(value, float_regex);
}

bool DataTypeInferrer::is_boolean(const std::string& value) {
    std::string lower_value = value;
    std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
    
    return lower_value == "true" || lower_value == "false" || 
           lower_value == "1" || lower_value == "0" ||
           lower_value == "yes" || lower_value == "no";
}

std::unordered_map<std::string, DataType> DataTypeInferrer::infer_schema_from_samples(
    const std::vector<std::unordered_map<std::string, std::string>>& samples) {
    
    std::unordered_map<std::string, std::unordered_map<DataType, int>> field_type_votes;
    
    // 收集每个字段的类型投票
    for (const auto& record : samples) {
        for (const auto& [field_name, field_value] : record) {
            DataType inferred_type = infer_type(field_value);
            field_type_votes[field_name][inferred_type]++;
        }
    }
    
    // 选择每个字段的最佳类型
    std::unordered_map<std::string, DataType> result;
    for (const auto& [field_name, type_votes] : field_type_votes) {
        DataType best_type = DataType::STRING;
        int max_votes = 0;
        
        for (const auto& [type, votes] : type_votes) {
            if (votes > max_votes) {
                max_votes = votes;
                best_type = type;
            }
        }
        
        result[field_name] = best_type;
    }
    
    return result;
}

// SchemaOptimizer 实现
std::unique_ptr<Schema> SchemaOptimizer::generate_optimized_schema(
    const std::string& schema_name,
    const std::vector<std::unordered_map<std::string, std::string>>& sample_data) {
    
    auto schema = std::make_unique<Schema>(schema_name);
    
    if (sample_data.empty()) {
        std::cout << "[SchemaOptimizer] 警告: 没有样本数据，无法生成优化Schema\n";
        return schema;
    }
    
    // 分析数据特征
    auto characteristics = analyze_data(sample_data);
    
    // 推断字段类型
    auto field_types = DataTypeInferrer::infer_schema_from_samples(sample_data);
    
    // 按访问频率排序字段
    std::vector<std::pair<std::string, size_t>> field_freq_pairs;
    for (const auto& [field_name, frequency] : characteristics.field_frequencies) {
        field_freq_pairs.emplace_back(field_name, frequency);
    }
    
    std::sort(field_freq_pairs.begin(), field_freq_pairs.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second; // 按频率降序
              });
    
    // 生成优化的字段定义
    uint32_t field_number = 1;
    for (const auto& [field_name, frequency] : field_freq_pairs) {
        DataType field_type = field_types[field_name];
        bool is_optional = frequency < characteristics.total_records; // 不是所有记录都有的字段
        
        schema->add_field(field_name, field_type, field_number++, false, is_optional);
    }
    
    schema->optimize_field_order();
    
    std::cout << "[SchemaOptimizer] 生成优化Schema: " << schema_name 
              << " (字段数: " << schema->field_count() << ")\n";
    
    return schema;
}

void SchemaOptimizer::optimize_schema(Schema& schema, 
                                     const std::vector<std::unordered_map<std::string, std::string>>& sample_data) {
    schema.suggest_data_types(sample_data);
    schema.optimize_field_order();
}

SchemaOptimizer::DataCharacteristics SchemaOptimizer::analyze_data(
    const std::vector<std::unordered_map<std::string, std::string>>& sample_data) {
    
    DataCharacteristics characteristics;
    characteristics.total_records = sample_data.size();
    
    size_t total_text_size = 0;
    size_t total_binary_size = 0;
    
    for (const auto& record : sample_data) {
        for (const auto& [field_name, field_value] : record) {
            // 统计字段频率
            characteristics.field_frequencies[field_name]++;
            
            // 统计字段大小
            characteristics.field_sizes[field_name] += field_value.size();
            
            // 累计文本大小
            total_text_size += field_name.size() + field_value.size() + 2; // +2 for separators
            
            // 估算二进制大小
            DataType inferred_type = DataTypeInferrer::infer_type(field_value);
            switch (inferred_type) {
                case DataType::BOOL:
                    total_binary_size += 1;
                    break;
                case DataType::VARINT:
                case DataType::INT32:
                case DataType::UINT32:
                    total_binary_size += 4; // 平均大小
                    break;
                case DataType::INT64:
                case DataType::UINT64:
                case DataType::DOUBLE:
                    total_binary_size += 8;
                    break;
                case DataType::FLOAT:
                    total_binary_size += 4;
                    break;
                default: // STRING
                    total_binary_size += field_value.size() + 2; // +2 for length prefix
                    break;
            }
        }
    }
    
    // 推断最佳数据类型
    characteristics.recommended_types = DataTypeInferrer::infer_schema_from_samples(sample_data);
    
    // 计算压缩潜力
    characteristics.compression_potential = total_text_size > 0 ? 
        1.0 - (double)total_binary_size / total_text_size : 0.0;
    
    return characteristics;
}

void SchemaOptimizer::print_analysis_report(const DataCharacteristics& characteristics) {
    std::cout << "\n=== 数据特征分析报告 ===\n";
    std::cout << "总记录数: " << characteristics.total_records << "\n";
    std::cout << "字段数量: " << characteristics.field_frequencies.size() << "\n";
    std::cout << "压缩潜力: " << (characteristics.compression_potential * 100) << "%\n";
    
    std::cout << "\n字段频率统计:\n";
    std::vector<std::pair<std::string, size_t>> freq_pairs;
    for (const auto& [field, freq] : characteristics.field_frequencies) {
        freq_pairs.emplace_back(field, freq);
    }
    
    std::sort(freq_pairs.begin(), freq_pairs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    for (const auto& [field, freq] : freq_pairs) {
        double coverage = (double)freq / characteristics.total_records * 100;
        std::cout << "  " << field << ": " << freq << " (" << coverage << "%)\n";
    }
    
    std::cout << "\n推荐数据类型:\n";
    for (const auto& [field, type] : characteristics.recommended_types) {
        std::cout << "  " << field << ": " << static_cast<int>(type) << "\n";
    }
    
    std::cout << "========================\n\n";
}

void SchemaOptimizer::update_field_access(const std::string& field_name) {
    field_access_counts_[field_name]++;
}

std::vector<std::string> SchemaOptimizer::get_fields_by_frequency() const {
    std::vector<std::pair<std::string, uint64_t>> field_pairs;
    for (const auto& [field, count] : field_access_counts_) {
        field_pairs.emplace_back(field, count);
    }
    
    std::sort(field_pairs.begin(), field_pairs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::vector<std::string> result;
    for (const auto& [field, count] : field_pairs) {
        result.push_back(field);
    }
    
    return result;
}