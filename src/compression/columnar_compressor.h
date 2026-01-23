#pragma once
#include "compression_interface.h"
#include <map>
#include <vector>

// 列式压缩器（相同key的value压缩）
class ColumnarCompressor : public Compressor {
public:
    ColumnarCompressor();
    ~ColumnarCompressor() override = default;
    
    std::optional<std::string> compress(const std::string& data) override;
    std::optional<std::string> decompress(const std::string& compressed_data) override;
    
    // 批量压缩（更适合列式压缩）
    std::optional<std::string> compress_batch(const std::vector<std::pair<std::string, std::string>>& kv_pairs);
    std::vector<std::pair<std::string, std::string>> decompress_batch(const std::string& compressed_data);
    
    CompressionType get_type() const override { return CompressionType::COLUMNAR; }
    const CompressionStats& get_stats() const override { return stats_; }
    void reset_stats() override;
    std::string get_name() const override { return "Columnar"; }
    
    // 配置参数
    void set_min_group_size(size_t size) { min_group_size_ = size; }
    void set_max_group_size(size_t size) { max_group_size_ = size; }
    
private:
    mutable CompressionStats stats_;
    size_t min_group_size_ = 3;   // 最小分组大小
    size_t max_group_size_ = 100; // 最大分组大小
    
    // 列式压缩核心算法
    struct ColumnGroup {
        std::string key_pattern;
        std::vector<std::string> values;
        std::string compressed_values;
    };
    
    std::vector<ColumnGroup> group_by_key_pattern(const std::vector<std::pair<std::string, std::string>>& kv_pairs);
    std::string compress_value_column(const std::vector<std::string>& values);
    std::vector<std::string> decompress_value_column(const std::string& compressed_values);
    
    std::string extract_key_pattern(const std::string& key);
    std::string serialize_column_groups(const std::vector<ColumnGroup>& groups);
    std::vector<ColumnGroup> deserialize_column_groups(const std::string& data);
    
    void update_compression_stats(uint64_t original_size, uint64_t compressed_size, uint64_t time_us);
    void update_decompression_stats(uint64_t time_us);
};