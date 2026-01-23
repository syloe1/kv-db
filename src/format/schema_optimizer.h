#pragma once
#include "binary_encoder.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

// 字段描述
struct FieldDescriptor {
    std::string name;
    DataType type;
    bool is_repeated;
    bool is_optional;
    uint32_t field_number;
    
    FieldDescriptor(const std::string& n, DataType t, uint32_t num, 
                   bool repeated = false, bool optional = false)
        : name(n), type(t), is_repeated(repeated), is_optional(optional), field_number(num) {}
};

// Schema定义
class Schema {
public:
    Schema(const std::string& name) : name_(name) {}
    
    void add_field(const FieldDescriptor& field);
    void add_field(const std::string& name, DataType type, uint32_t field_number, 
                   bool repeated = false, bool optional = false);
    
    const std::vector<FieldDescriptor>& get_fields() const { return fields_; }
    const FieldDescriptor* get_field(uint32_t field_number) const;
    const FieldDescriptor* get_field(const std::string& name) const;
    
    std::string get_name() const { return name_; }
    size_t field_count() const { return fields_.size(); }
    
    // Schema优化
    void optimize_field_order(); // 按访问频率重排字段
    void suggest_data_types(const std::vector<std::unordered_map<std::string, std::string>>& sample_data);
    
private:
    std::string name_;
    std::vector<FieldDescriptor> fields_;
    std::unordered_map<std::string, size_t> field_name_to_index_;
    std::unordered_map<uint32_t, size_t> field_number_to_index_;
};

// 数据类型推断器
class DataTypeInferrer {
public:
    static DataType infer_type(const std::string& value);
    static DataType infer_optimal_int_type(int64_t value);
    static bool is_numeric(const std::string& value);
    static bool is_integer(const std::string& value);
    static bool is_float(const std::string& value);
    static bool is_boolean(const std::string& value);
    
    // 批量推断
    static std::unordered_map<std::string, DataType> infer_schema_from_samples(
        const std::vector<std::unordered_map<std::string, std::string>>& samples);
};

// Schema优化器
class SchemaOptimizer {
public:
    SchemaOptimizer() = default;
    
    // 从样本数据生成优化的Schema
    std::unique_ptr<Schema> generate_optimized_schema(
        const std::string& schema_name,
        const std::vector<std::unordered_map<std::string, std::string>>& sample_data);
    
    // 优化现有Schema
    void optimize_schema(Schema& schema, 
                        const std::vector<std::unordered_map<std::string, std::string>>& sample_data);
    
    // 分析数据特征
    struct DataCharacteristics {
        size_t total_records;
        std::unordered_map<std::string, size_t> field_frequencies;
        std::unordered_map<std::string, size_t> field_sizes;
        std::unordered_map<std::string, DataType> recommended_types;
        double compression_potential;
    };
    
    DataCharacteristics analyze_data(
        const std::vector<std::unordered_map<std::string, std::string>>& sample_data);
    
    void print_analysis_report(const DataCharacteristics& characteristics);
    
private:
    // 字段访问频率统计
    std::unordered_map<std::string, uint64_t> field_access_counts_;
    
    void update_field_access(const std::string& field_name);
    std::vector<std::string> get_fields_by_frequency() const;
};