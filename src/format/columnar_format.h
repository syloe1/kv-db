#pragma once
#include "binary_encoder.h"
#include "schema_optimizer.h"
#include <vector>
#include <unordered_map>
#include <memory>

// 列数据块
struct ColumnBlock {
    std::string column_name;
    DataType data_type;
    std::vector<uint8_t> data;
    std::vector<bool> null_bitmap; // 空值位图
    size_t row_count;
    
    ColumnBlock(const std::string& name, DataType type) 
        : column_name(name), data_type(type), row_count(0) {}
};

// 列式存储写入器
class ColumnarWriter {
public:
    explicit ColumnarWriter(std::shared_ptr<Schema> schema);
    ~ColumnarWriter() = default;
    
    // 添加记录
    bool add_record(const std::unordered_map<std::string, std::string>& record);
    bool add_record_binary(const std::unordered_map<std::string, std::vector<uint8_t>>& record);
    
    // 批量添加
    bool add_records(const std::vector<std::unordered_map<std::string, std::string>>& records);
    
    // 完成写入并获取结果
    std::vector<uint8_t> finalize();
    
    // 统计信息
    size_t get_record_count() const { return record_count_; }
    size_t get_column_count() const { return columns_.size(); }
    
    void print_statistics() const;
    
private:
    std::shared_ptr<Schema> schema_;
    std::vector<std::unique_ptr<ColumnBlock>> columns_;
    std::unordered_map<std::string, size_t> column_name_to_index_;
    size_t record_count_;
    
    void initialize_columns();
    bool encode_value_to_column(const std::string& column_name, const std::string& value);
    std::vector<uint8_t> serialize_column_block(const ColumnBlock& block);
    std::vector<uint8_t> serialize_header();
};

// 列式存储读取器
class ColumnarReader {
public:
    ColumnarReader() = default;
    ~ColumnarReader() = default;
    
    // 加载数据
    bool load_data(const std::vector<uint8_t>& data);
    bool load_data(const uint8_t* data, size_t size);
    
    // 读取记录
    std::optional<std::unordered_map<std::string, std::string>> read_record(size_t index);
    std::vector<std::unordered_map<std::string, std::string>> read_all_records();
    
    // 列式查询
    std::optional<std::vector<std::string>> read_column(const std::string& column_name);
    std::vector<std::unordered_map<std::string, std::string>> read_records_where(
        const std::string& column_name, const std::string& value);
    
    // 信息查询
    size_t get_record_count() const { return record_count_; }
    size_t get_column_count() const { return columns_.size(); }
    std::vector<std::string> get_column_names() const;
    std::shared_ptr<Schema> get_schema() const { return schema_; }
    
    void print_statistics() const;
    
private:
    std::shared_ptr<Schema> schema_;
    std::vector<std::unique_ptr<ColumnBlock>> columns_;
    std::unordered_map<std::string, size_t> column_name_to_index_;
    size_t record_count_;
    
    bool deserialize_header(BinaryDecoder& decoder);
    bool deserialize_column_block(BinaryDecoder& decoder, ColumnBlock& block);
    std::string decode_value_from_column(const ColumnBlock& block, size_t index);
};

// 列式格式管理器
class ColumnarFormatManager {
public:
    ColumnarFormatManager() = default;
    
    // 文本到列式格式转换
    struct ConversionResult {
        std::vector<uint8_t> columnar_data;
        size_t original_size;
        size_t compressed_size;
        double compression_ratio;
        uint64_t conversion_time_us;
        std::shared_ptr<Schema> schema;
    };
    
    ConversionResult convert_to_columnar(
        const std::vector<std::unordered_map<std::string, std::string>>& records,
        const std::string& schema_name = "auto_generated");
    
    // 列式格式到文本转换
    struct DeconversionResult {
        std::vector<std::unordered_map<std::string, std::string>> records;
        uint64_t deconversion_time_us;
        bool success;
    };
    
    DeconversionResult convert_from_columnar(const std::vector<uint8_t>& columnar_data);
    
    // 性能测试
    struct PerformanceComparison {
        size_t text_format_size;
        size_t columnar_format_size;
        double space_savings;
        uint64_t text_parse_time_us;
        uint64_t columnar_parse_time_us;
        double parse_speed_improvement;
    };
    
    PerformanceComparison compare_formats(
        const std::vector<std::unordered_map<std::string, std::string>>& test_data);
    
    void print_comparison_report(const PerformanceComparison& comparison);
    
private:
    SchemaOptimizer schema_optimizer_;
    
    // 文本格式性能测试
    uint64_t benchmark_text_parsing(
        const std::vector<std::unordered_map<std::string, std::string>>& records);
    
    size_t calculate_text_size(
        const std::vector<std::unordered_map<std::string, std::string>>& records);
};