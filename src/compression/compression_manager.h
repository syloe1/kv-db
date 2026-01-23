#pragma once
#include "compression_interface.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

// 压缩管理器：统一管理不同的压缩算法
class CompressionManager {
public:
    CompressionManager();
    ~CompressionManager() = default;
    
    // 设置默认压缩算法
    void set_default_compression(CompressionType type);
    CompressionType get_default_compression() const { return default_type_; }
    
    // 压缩/解压接口
    std::optional<std::string> compress(const std::string& data, CompressionType type = CompressionType::NONE);
    std::optional<std::string> decompress(const std::string& compressed_data, CompressionType type);
    
    // 自动选择最佳压缩算法
    struct CompressionResult {
        CompressionType type;
        std::string compressed_data;
        double compression_ratio;
        uint64_t compression_time_us;
    };
    
    CompressionResult compress_with_best_algorithm(const std::string& data);
    
    // 批量压缩（适用于列式压缩）
    std::optional<std::string> compress_batch(
        const std::vector<std::pair<std::string, std::string>>& kv_pairs,
        CompressionType type = CompressionType::COLUMNAR);
    
    std::vector<std::pair<std::string, std::string>> decompress_batch(
        const std::string& compressed_data, CompressionType type);
    
    // 压缩算法性能测试
    struct BenchmarkResult {
        CompressionType type;
        std::string name;
        double compression_ratio;
        double compression_speed_mbps;
        double decompression_speed_mbps;
        uint64_t compression_time_us;
        uint64_t decompression_time_us;
    };
    
    std::vector<BenchmarkResult> benchmark_all_algorithms(const std::string& test_data);
    void print_benchmark_results(const std::vector<BenchmarkResult>& results);
    
    // 统计信息
    void print_all_stats() const;
    void reset_all_stats();
    
    // 配置管理
    void set_zstd_compression_level(int level);
    void set_columnar_min_group_size(size_t size);
    
private:
    CompressionType default_type_;
    std::unordered_map<CompressionType, std::unique_ptr<Compressor>> compressors_;
    
    void initialize_compressors();
    Compressor* get_compressor(CompressionType type);
    
    // 自动选择算法的策略
    CompressionType select_best_algorithm_for_data(const std::string& data);
    bool is_text_data(const std::string& data);
    bool has_repetitive_patterns(const std::string& data);
};