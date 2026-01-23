#pragma once
#include <string>
#include <memory>
#include <vector>
#include <optional>

// 压缩算法类型
enum class CompressionType {
    NONE = 0,
    SNAPPY = 1,
    LZ4 = 2,
    ZSTD = 3,
    COLUMNAR = 4
};

// 压缩统计信息
struct CompressionStats {
    uint64_t original_size = 0;
    uint64_t compressed_size = 0;
    uint64_t compression_time_us = 0;
    uint64_t decompression_time_us = 0;
    uint32_t compression_count = 0;
    uint32_t decompression_count = 0;
    
    double get_compression_ratio() const {
        return original_size > 0 ? (double)compressed_size / original_size : 1.0;
    }
    
    double get_space_savings() const {
        return original_size > 0 ? (1.0 - get_compression_ratio()) * 100.0 : 0.0;
    }
};

// 压缩器接口
class Compressor {
public:
    virtual ~Compressor() = default;
    
    // 压缩数据
    virtual std::optional<std::string> compress(const std::string& data) = 0;
    
    // 解压数据
    virtual std::optional<std::string> decompress(const std::string& compressed_data) = 0;
    
    // 获取压缩类型
    virtual CompressionType get_type() const = 0;
    
    // 获取统计信息
    virtual const CompressionStats& get_stats() const = 0;
    
    // 重置统计信息
    virtual void reset_stats() = 0;
    
    // 获取压缩器名称
    virtual std::string get_name() const = 0;
};

// 压缩器工厂
class CompressionFactory {
public:
    static std::unique_ptr<Compressor> create_compressor(CompressionType type);
    static std::vector<CompressionType> get_available_types();
    static std::string type_to_string(CompressionType type);
    static CompressionType string_to_type(const std::string& type_str);
};