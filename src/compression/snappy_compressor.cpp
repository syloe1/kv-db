#include "snappy_compressor.h"
#include <iostream>
#include <algorithm>
#include <unordered_map>

// SnappyCompressor 实现
SnappyCompressor::SnappyCompressor() {
    std::cout << "[SnappyCompressor] 初始化Snappy压缩器（快速压缩）\n";
}

std::optional<std::string> SnappyCompressor::compress(const std::string& data) {
    if (data.empty()) return data;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        std::string compressed = simple_snappy_compress(data);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        update_compression_stats(data.size(), compressed.size(), duration);
        return compressed;
        
    } catch (const std::exception& e) {
        std::cerr << "[SnappyCompressor] 压缩失败: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::optional<std::string> SnappyCompressor::decompress(const std::string& compressed_data) {
    if (compressed_data.empty()) return compressed_data;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        std::string decompressed = simple_snappy_decompress(compressed_data);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        update_decompression_stats(duration);
        return decompressed;
        
    } catch (const std::exception& e) {
        std::cerr << "[SnappyCompressor] 解压失败: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::string SnappyCompressor::simple_snappy_compress(const std::string& data) {
    // 简化的Snappy算法：基于重复字符串的压缩
    std::string result;
    result.reserve(data.size());
    
    size_t i = 0;
    while (i < data.size()) {
        // 查找重复的字符序列
        size_t best_length = 0;
        size_t best_distance = 0;
        
        // 向前查找最多64字节
        for (size_t distance = 1; distance <= std::min(i, size_t(64)); ++distance) {
            size_t length = 0;
            while (i + length < data.size() && 
                   data[i + length] == data[i - distance + length] && 
                   length < 64) {
                length++;
            }
            
            if (length > best_length && length >= 4) {
                best_length = length;
                best_distance = distance;
            }
        }
        
        if (best_length >= 4) {
            // 编码重复序列：标记位 + 距离 + 长度
            result.push_back(0xFF); // 重复序列标记
            result.push_back(static_cast<char>(best_distance));
            result.push_back(static_cast<char>(best_length));
            i += best_length;
        } else {
            // 直接复制字符
            result.push_back(data[i]);
            i++;
        }
    }
    
    return result;
}

std::string SnappyCompressor::simple_snappy_decompress(const std::string& compressed_data) {
    std::string result;
    result.reserve(compressed_data.size() * 2); // 预估解压后大小
    
    size_t i = 0;
    while (i < compressed_data.size()) {
        if (compressed_data[i] == static_cast<char>(0xFF) && i + 2 < compressed_data.size()) {
            // 解码重复序列
            size_t distance = static_cast<unsigned char>(compressed_data[i + 1]);
            size_t length = static_cast<unsigned char>(compressed_data[i + 2]);
            
            // 复制之前的数据
            for (size_t j = 0; j < length; ++j) {
                if (result.size() >= distance) {
                    result.push_back(result[result.size() - distance]);
                }
            }
            
            i += 3;
        } else {
            // 直接复制字符
            result.push_back(compressed_data[i]);
            i++;
        }
    }
    
    return result;
}

void SnappyCompressor::update_compression_stats(uint64_t original_size, uint64_t compressed_size, uint64_t time_us) {
    stats_.original_size += original_size;
    stats_.compressed_size += compressed_size;
    stats_.compression_time_us += time_us;
    stats_.compression_count++;
}

void SnappyCompressor::update_decompression_stats(uint64_t time_us) {
    stats_.decompression_time_us += time_us;
    stats_.decompression_count++;
}

void SnappyCompressor::reset_stats() {
    stats_ = CompressionStats{};
}

// LZ4Compressor 实现
LZ4Compressor::LZ4Compressor() {
    std::cout << "[LZ4Compressor] 初始化LZ4压缩器（高压缩比）\n";
}

std::optional<std::string> LZ4Compressor::compress(const std::string& data) {
    if (data.empty()) return data;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        std::string compressed = simple_lz4_compress(data);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        update_compression_stats(data.size(), compressed.size(), duration);
        return compressed;
        
    } catch (const std::exception& e) {
        std::cerr << "[LZ4Compressor] 压缩失败: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::optional<std::string> LZ4Compressor::decompress(const std::string& compressed_data) {
    if (compressed_data.empty()) return compressed_data;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        std::string decompressed = simple_lz4_decompress(compressed_data);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        update_decompression_stats(duration);
        return decompressed;
        
    } catch (const std::exception& e) {
        std::cerr << "[LZ4Compressor] 解压失败: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::string LZ4Compressor::simple_lz4_compress(const std::string& data) {
    // 简化的LZ4算法：更激进的重复序列查找
    std::string result;
    result.reserve(data.size());
    
    std::unordered_map<std::string, size_t> hash_table;
    
    size_t i = 0;
    while (i < data.size()) {
        size_t best_length = 0;
        size_t best_position = 0;
        
        // 使用哈希表查找重复序列
        for (size_t len = 4; len <= std::min(data.size() - i, size_t(255)); ++len) {
            std::string substr = data.substr(i, len);
            auto it = hash_table.find(substr);
            
            if (it != hash_table.end() && i - it->second <= 65535) {
                best_length = len;
                best_position = it->second;
            }
        }
        
        if (best_length >= 4) {
            // 编码重复序列：标记 + 距离(2字节) + 长度
            result.push_back(0xFE); // LZ4重复序列标记
            uint16_t distance = static_cast<uint16_t>(i - best_position);
            result.push_back(static_cast<char>(distance & 0xFF));
            result.push_back(static_cast<char>((distance >> 8) & 0xFF));
            result.push_back(static_cast<char>(best_length));
            
            // 更新哈希表
            for (size_t j = 0; j < best_length; ++j) {
                if (i + j + 4 <= data.size()) {
                    hash_table[data.substr(i + j, 4)] = i + j;
                }
            }
            
            i += best_length;
        } else {
            // 更新哈希表并直接复制字符
            if (i + 4 <= data.size()) {
                hash_table[data.substr(i, 4)] = i;
            }
            result.push_back(data[i]);
            i++;
        }
    }
    
    return result;
}

std::string LZ4Compressor::simple_lz4_decompress(const std::string& compressed_data) {
    std::string result;
    result.reserve(compressed_data.size() * 3); // 预估解压后大小
    
    size_t i = 0;
    while (i < compressed_data.size()) {
        if (compressed_data[i] == static_cast<char>(0xFE) && i + 3 < compressed_data.size()) {
            // 解码重复序列
            uint16_t distance = static_cast<unsigned char>(compressed_data[i + 1]) |
                               (static_cast<unsigned char>(compressed_data[i + 2]) << 8);
            size_t length = static_cast<unsigned char>(compressed_data[i + 3]);
            
            // 复制之前的数据
            for (size_t j = 0; j < length; ++j) {
                if (result.size() >= distance) {
                    result.push_back(result[result.size() - distance]);
                }
            }
            
            i += 4;
        } else {
            // 直接复制字符
            result.push_back(compressed_data[i]);
            i++;
        }
    }
    
    return result;
}

void LZ4Compressor::update_compression_stats(uint64_t original_size, uint64_t compressed_size, uint64_t time_us) {
    stats_.original_size += original_size;
    stats_.compressed_size += compressed_size;
    stats_.compression_time_us += time_us;
    stats_.compression_count++;
}

void LZ4Compressor::update_decompression_stats(uint64_t time_us) {
    stats_.decompression_time_us += time_us;
    stats_.decompression_count++;
}

void LZ4Compressor::reset_stats() {
    stats_ = CompressionStats{};
}

// ZSTDCompressor 实现
ZSTDCompressor::ZSTDCompressor(int compression_level) : compression_level_(compression_level) {
    std::cout << "[ZSTDCompressor] 初始化ZSTD压缩器（平衡压缩比和速度），级别: " << compression_level << "\n";
}

std::optional<std::string> ZSTDCompressor::compress(const std::string& data) {
    if (data.empty()) return data;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        std::string compressed = simple_zstd_compress(data);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        update_compression_stats(data.size(), compressed.size(), duration);
        return compressed;
        
    } catch (const std::exception& e) {
        std::cerr << "[ZSTDCompressor] 压缩失败: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::optional<std::string> ZSTDCompressor::decompress(const std::string& compressed_data) {
    if (compressed_data.empty()) return compressed_data;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        std::string decompressed = simple_zstd_decompress(compressed_data);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        update_decompression_stats(duration);
        return decompressed;
        
    } catch (const std::exception& e) {
        std::cerr << "[ZSTDCompressor] 解压失败: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::string ZSTDCompressor::simple_zstd_compress(const std::string& data) {
    // 简化的ZSTD算法：结合字典压缩和重复序列查找
    std::string result;
    result.reserve(data.size());
    
    // 构建字符频率表
    std::unordered_map<char, uint32_t> freq_table;
    for (char c : data) {
        freq_table[c]++;
    }
    
    // 根据压缩级别调整查找窗口大小
    size_t window_size = 1024 * compression_level_;
    
    size_t i = 0;
    while (i < data.size()) {
        size_t best_length = 0;
        size_t best_distance = 0;
        
        // 在窗口内查找最长匹配
        size_t start_pos = i > window_size ? i - window_size : 0;
        for (size_t pos = start_pos; pos < i; ++pos) {
            size_t length = 0;
            while (i + length < data.size() && 
                   pos + length < i &&
                   data[i + length] == data[pos + length] && 
                   length < 255) {
                length++;
            }
            
            if (length > best_length && length >= 3) {
                best_length = length;
                best_distance = i - pos;
            }
        }
        
        if (best_length >= 3) {
            // 编码重复序列：ZSTD标记 + 距离 + 长度
            result.push_back(0xFD); // ZSTD重复序列标记
            result.push_back(static_cast<char>(best_distance & 0xFF));
            result.push_back(static_cast<char>((best_distance >> 8) & 0xFF));
            result.push_back(static_cast<char>(best_length));
            i += best_length;
        } else {
            // 字符频率编码（简化版）
            if (freq_table[data[i]] > data.size() / 10) {
                // 高频字符用特殊编码
                result.push_back(0xFC);
                result.push_back(data[i]);
            } else {
                result.push_back(data[i]);
            }
            i++;
        }
    }
    
    return result;
}

std::string ZSTDCompressor::simple_zstd_decompress(const std::string& compressed_data) {
    std::string result;
    result.reserve(compressed_data.size() * 4); // 预估解压后大小
    
    size_t i = 0;
    while (i < compressed_data.size()) {
        if (compressed_data[i] == static_cast<char>(0xFD) && i + 3 < compressed_data.size()) {
            // 解码重复序列
            uint16_t distance = static_cast<unsigned char>(compressed_data[i + 1]) |
                               (static_cast<unsigned char>(compressed_data[i + 2]) << 8);
            size_t length = static_cast<unsigned char>(compressed_data[i + 3]);
            
            // 复制之前的数据
            for (size_t j = 0; j < length; ++j) {
                if (result.size() >= distance) {
                    result.push_back(result[result.size() - distance]);
                }
            }
            
            i += 4;
        } else if (compressed_data[i] == static_cast<char>(0xFC) && i + 1 < compressed_data.size()) {
            // 解码高频字符
            result.push_back(compressed_data[i + 1]);
            i += 2;
        } else {
            // 直接复制字符
            result.push_back(compressed_data[i]);
            i++;
        }
    }
    
    return result;
}

void ZSTDCompressor::update_compression_stats(uint64_t original_size, uint64_t compressed_size, uint64_t time_us) {
    stats_.original_size += original_size;
    stats_.compressed_size += compressed_size;
    stats_.compression_time_us += time_us;
    stats_.compression_count++;
}

void ZSTDCompressor::update_decompression_stats(uint64_t time_us) {
    stats_.decompression_time_us += time_us;
    stats_.decompression_count++;
}

void ZSTDCompressor::reset_stats() {
    stats_ = CompressionStats{};
}