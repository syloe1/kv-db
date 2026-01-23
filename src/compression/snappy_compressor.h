#pragma once
#include "compression_interface.h"
#include <chrono>

// Snappy压缩器实现（快速压缩，适合实时场景）
class SnappyCompressor : public Compressor {
public:
    SnappyCompressor();
    ~SnappyCompressor() override = default;
    
    std::optional<std::string> compress(const std::string& data) override;
    std::optional<std::string> decompress(const std::string& compressed_data) override;
    
    CompressionType get_type() const override { return CompressionType::SNAPPY; }
    const CompressionStats& get_stats() const override { return stats_; }
    void reset_stats() override;
    std::string get_name() const override { return "Snappy"; }
    
private:
    mutable CompressionStats stats_;
    
    // 简化的Snappy算法实现
    std::string simple_snappy_compress(const std::string& data);
    std::string simple_snappy_decompress(const std::string& compressed_data);
    
    void update_compression_stats(uint64_t original_size, uint64_t compressed_size, uint64_t time_us);
    void update_decompression_stats(uint64_t time_us);
};

// LZ4压缩器实现（高压缩比，适合存储）
class LZ4Compressor : public Compressor {
public:
    LZ4Compressor();
    ~LZ4Compressor() override = default;
    
    std::optional<std::string> compress(const std::string& data) override;
    std::optional<std::string> decompress(const std::string& compressed_data) override;
    
    CompressionType get_type() const override { return CompressionType::LZ4; }
    const CompressionStats& get_stats() const override { return stats_; }
    void reset_stats() override;
    std::string get_name() const override { return "LZ4"; }
    
private:
    mutable CompressionStats stats_;
    
    // 简化的LZ4算法实现
    std::string simple_lz4_compress(const std::string& data);
    std::string simple_lz4_decompress(const std::string& compressed_data);
    
    void update_compression_stats(uint64_t original_size, uint64_t compressed_size, uint64_t time_us);
    void update_decompression_stats(uint64_t time_us);
};

// ZSTD压缩器实现（平衡压缩比和速度）
class ZSTDCompressor : public Compressor {
public:
    explicit ZSTDCompressor(int compression_level = 3);
    ~ZSTDCompressor() override = default;
    
    std::optional<std::string> compress(const std::string& data) override;
    std::optional<std::string> decompress(const std::string& compressed_data) override;
    
    CompressionType get_type() const override { return CompressionType::ZSTD; }
    const CompressionStats& get_stats() const override { return stats_; }
    void reset_stats() override;
    std::string get_name() const override { return "ZSTD"; }
    
    void set_compression_level(int level) { compression_level_ = level; }
    int get_compression_level() const { return compression_level_; }
    
private:
    mutable CompressionStats stats_;
    int compression_level_;
    
    // 简化的ZSTD算法实现
    std::string simple_zstd_compress(const std::string& data);
    std::string simple_zstd_decompress(const std::string& compressed_data);
    
    void update_compression_stats(uint64_t original_size, uint64_t compressed_size, uint64_t time_us);
    void update_decompression_stats(uint64_t time_us);
};

// 无压缩器（用于对比）
class NoCompressor : public Compressor {
public:
    NoCompressor() = default;
    ~NoCompressor() override = default;
    
    std::optional<std::string> compress(const std::string& data) override {
        return data;
    }
    
    std::optional<std::string> decompress(const std::string& compressed_data) override {
        return compressed_data;
    }
    
    CompressionType get_type() const override { return CompressionType::NONE; }
    const CompressionStats& get_stats() const override { return stats_; }
    void reset_stats() override { stats_ = CompressionStats{}; }
    std::string get_name() const override { return "None"; }
    
private:
    mutable CompressionStats stats_;
};