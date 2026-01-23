#include "compression_manager.h"
#include "snappy_compressor.h"
#include "columnar_compressor.h"
#include <iostream>
#include <algorithm>
#include <chrono>

CompressionManager::CompressionManager() : default_type_(CompressionType::SNAPPY) {
    initialize_compressors();
    std::cout << "[CompressionManager] 初始化压缩管理器，默认算法: " 
              << CompressionFactory::type_to_string(default_type_) << "\n";
}

void CompressionManager::initialize_compressors() {
    auto types = CompressionFactory::get_available_types();
    
    for (auto type : types) {
        compressors_[type] = CompressionFactory::create_compressor(type);
    }
    
    std::cout << "[CompressionManager] 已加载 " << compressors_.size() << " 种压缩算法\n";
}

void CompressionManager::set_default_compression(CompressionType type) {
    default_type_ = type;
    std::cout << "[CompressionManager] 设置默认压缩算法: " 
              << CompressionFactory::type_to_string(type) << "\n";
}

std::optional<std::string> CompressionManager::compress(const std::string& data, CompressionType type) {
    if (type == CompressionType::NONE) {
        type = default_type_;
    }
    
    auto* compressor = get_compressor(type);
    if (!compressor) {
        std::cerr << "[CompressionManager] 找不到压缩器: " 
                  << CompressionFactory::type_to_string(type) << "\n";
        return std::nullopt;
    }
    
    return compressor->compress(data);
}

std::optional<std::string> CompressionManager::decompress(const std::string& compressed_data, CompressionType type) {
    auto* compressor = get_compressor(type);
    if (!compressor) {
        std::cerr << "[CompressionManager] 找不到解压器: " 
                  << CompressionFactory::type_to_string(type) << "\n";
        return std::nullopt;
    }
    
    return compressor->decompress(compressed_data);
}

CompressionManager::CompressionResult CompressionManager::compress_with_best_algorithm(const std::string& data) {
    CompressionResult best_result;
    best_result.compression_ratio = 1.0;
    best_result.type = CompressionType::NONE;
    best_result.compressed_data = data;
    
    // 测试所有压缩算法，选择最佳的
    for (const auto& [type, compressor] : compressors_) {
        if (type == CompressionType::NONE) continue;
        
        auto start = std::chrono::high_resolution_clock::now();
        auto compressed = compressor->compress(data);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (compressed.has_value()) {
            double ratio = (double)compressed->size() / data.size();
            uint64_t time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            
            // 综合考虑压缩比和速度
            double score = ratio + (time_us / 1000000.0) * 0.1; // 时间权重较小
            double best_score = best_result.compression_ratio + (best_result.compression_time_us / 1000000.0) * 0.1;
            
            if (score < best_score) {
                best_result.type = type;
                best_result.compressed_data = *compressed;
                best_result.compression_ratio = ratio;
                best_result.compression_time_us = time_us;
            }
        }
    }
    
    std::cout << "[CompressionManager] 自动选择最佳算法: " 
              << CompressionFactory::type_to_string(best_result.type)
              << " (压缩比: " << best_result.compression_ratio << ")\n";
    
    return best_result;
}

std::optional<std::string> CompressionManager::compress_batch(
    const std::vector<std::pair<std::string, std::string>>& kv_pairs, CompressionType type) {
    
    if (type == CompressionType::COLUMNAR) {
        auto* columnar_compressor = dynamic_cast<ColumnarCompressor*>(get_compressor(type));
        if (columnar_compressor) {
            return columnar_compressor->compress_batch(kv_pairs);
        }
    }
    
    // 对于其他压缩算法，串联所有数据后压缩
    std::string combined_data;
    for (const auto& [key, value] : kv_pairs) {
        combined_data += key + ":" + value + "\n";
    }
    
    return compress(combined_data, type);
}

std::vector<std::pair<std::string, std::string>> CompressionManager::decompress_batch(
    const std::string& compressed_data, CompressionType type) {
    
    if (type == CompressionType::COLUMNAR) {
        auto* columnar_compressor = dynamic_cast<ColumnarCompressor*>(get_compressor(type));
        if (columnar_compressor) {
            return columnar_compressor->decompress_batch(compressed_data);
        }
    }
    
    // 对于其他压缩算法，先解压再分割
    auto decompressed = decompress(compressed_data, type);
    if (!decompressed.has_value()) {
        return {};
    }
    
    std::vector<std::pair<std::string, std::string>> result;
    std::istringstream iss(*decompressed);
    std::string line;
    
    while (std::getline(iss, line)) {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            result.emplace_back(key, value);
        }
    }
    
    return result;
}

std::vector<CompressionManager::BenchmarkResult> CompressionManager::benchmark_all_algorithms(const std::string& test_data) {
    std::vector<BenchmarkResult> results;
    
    std::cout << "[CompressionManager] 开始压缩算法性能测试，数据大小: " << test_data.size() << " bytes\n";
    
    for (const auto& [type, compressor] : compressors_) {
        BenchmarkResult result;
        result.type = type;
        result.name = compressor->get_name();
        
        // 压缩测试
        auto start = std::chrono::high_resolution_clock::now();
        auto compressed = compressor->compress(test_data);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (compressed.has_value()) {
            result.compression_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            result.compression_ratio = (double)compressed->size() / test_data.size();
            result.compression_speed_mbps = (double)test_data.size() / result.compression_time_us;
            
            // 解压测试
            start = std::chrono::high_resolution_clock::now();
            auto decompressed = compressor->decompress(*compressed);
            end = std::chrono::high_resolution_clock::now();
            
            if (decompressed.has_value()) {
                result.decompression_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                result.decompression_speed_mbps = (double)decompressed->size() / result.decompression_time_us;
                
                // 验证数据完整性
                if (*decompressed != test_data) {
                    std::cerr << "[CompressionManager] 数据完整性检查失败: " << result.name << "\n";
                }
            }
        }
        
        results.push_back(result);
    }
    
    // 按压缩比排序
    std::sort(results.begin(), results.end(), 
              [](const BenchmarkResult& a, const BenchmarkResult& b) {
                  return a.compression_ratio < b.compression_ratio;
              });
    
    return results;
}

void CompressionManager::print_benchmark_results(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n=== 压缩算法性能测试结果 ===\n";
    std::cout << "算法\t\t压缩比\t\t压缩速度(MB/s)\t解压速度(MB/s)\t压缩时间(μs)\t解压时间(μs)\n";
    std::cout << "-------------------------------------------------------------------------------------\n";
    
    for (const auto& result : results) {
        std::cout << result.name << "\t\t"
                  << std::fixed << std::setprecision(3) << result.compression_ratio << "\t\t"
                  << std::fixed << std::setprecision(1) << result.compression_speed_mbps << "\t\t"
                  << std::fixed << std::setprecision(1) << result.decompression_speed_mbps << "\t\t"
                  << result.compression_time_us << "\t\t"
                  << result.decompression_time_us << "\n";
    }
    
    std::cout << "===================================================================================\n\n";
    
    // 推荐最佳算法
    if (!results.empty()) {
        const auto& best_compression = *std::min_element(results.begin(), results.end(),
            [](const BenchmarkResult& a, const BenchmarkResult& b) {
                return a.compression_ratio < b.compression_ratio;
            });
        
        const auto& fastest_compression = *std::max_element(results.begin(), results.end(),
            [](const BenchmarkResult& a, const BenchmarkResult& b) {
                return a.compression_speed_mbps < b.compression_speed_mbps;
            });
        
        std::cout << "📊 推荐算法:\n";
        std::cout << "• 最佳压缩比: " << best_compression.name 
                  << " (压缩比: " << best_compression.compression_ratio << ")\n";
        std::cout << "• 最快压缩: " << fastest_compression.name 
                  << " (速度: " << fastest_compression.compression_speed_mbps << " MB/s)\n";
    }
}

void CompressionManager::print_all_stats() const {
    std::cout << "\n=== 所有压缩器统计信息 ===\n";
    
    for (const auto& [type, compressor] : compressors_) {
        const auto& stats = compressor->get_stats();
        std::cout << "\n" << compressor->get_name() << " 统计:\n";
        std::cout << "  压缩次数: " << stats.compression_count << "\n";
        std::cout << "  解压次数: " << stats.decompression_count << "\n";
        std::cout << "  原始大小: " << stats.original_size << " bytes\n";
        std::cout << "  压缩大小: " << stats.compressed_size << " bytes\n";
        std::cout << "  压缩比: " << std::fixed << std::setprecision(3) << stats.get_compression_ratio() << "\n";
        std::cout << "  空间节省: " << std::fixed << std::setprecision(1) << stats.get_space_savings() << "%\n";
        std::cout << "  压缩时间: " << stats.compression_time_us << " μs\n";
        std::cout << "  解压时间: " << stats.decompression_time_us << " μs\n";
    }
    
    std::cout << "============================\n\n";
}

void CompressionManager::reset_all_stats() {
    for (const auto& [type, compressor] : compressors_) {
        compressor->reset_stats();
    }
    std::cout << "[CompressionManager] 已重置所有压缩器统计信息\n";
}

void CompressionManager::set_zstd_compression_level(int level) {
    auto* zstd_compressor = dynamic_cast<ZSTDCompressor*>(get_compressor(CompressionType::ZSTD));
    if (zstd_compressor) {
        zstd_compressor->set_compression_level(level);
        std::cout << "[CompressionManager] 设置ZSTD压缩级别: " << level << "\n";
    }
}

void CompressionManager::set_columnar_min_group_size(size_t size) {
    auto* columnar_compressor = dynamic_cast<ColumnarCompressor*>(get_compressor(CompressionType::COLUMNAR));
    if (columnar_compressor) {
        columnar_compressor->set_min_group_size(size);
        std::cout << "[CompressionManager] 设置列式压缩最小分组大小: " << size << "\n";
    }
}

Compressor* CompressionManager::get_compressor(CompressionType type) {
    auto it = compressors_.find(type);
    return (it != compressors_.end()) ? it->second.get() : nullptr;
}

CompressionType CompressionManager::select_best_algorithm_for_data(const std::string& data) {
    // 简单的启发式算法选择
    if (data.size() < 100) {
        return CompressionType::NONE; // 小数据不压缩
    }
    
    if (is_text_data(data)) {
        if (has_repetitive_patterns(data)) {
            return CompressionType::LZ4; // 重复模式多用LZ4
        } else {
            return CompressionType::SNAPPY; // 文本数据用Snappy
        }
    } else {
        return CompressionType::ZSTD; // 二进制数据用ZSTD
    }
}

bool CompressionManager::is_text_data(const std::string& data) {
    // 简单检测是否为文本数据
    size_t printable_chars = 0;
    for (char c : data) {
        if (std::isprint(c) || std::isspace(c)) {
            printable_chars++;
        }
    }
    
    return (double)printable_chars / data.size() > 0.8;
}

bool CompressionManager::has_repetitive_patterns(const std::string& data) {
    // 简单检测是否有重复模式
    std::unordered_map<std::string, int> pattern_count;
    
    for (size_t i = 0; i + 4 <= data.size(); ++i) {
        std::string pattern = data.substr(i, 4);
        pattern_count[pattern]++;
    }
    
    // 如果有模式出现超过5次，认为有重复
    for (const auto& [pattern, count] : pattern_count) {
        if (count > 5) {
            return true;
        }
    }
    
    return false;
}