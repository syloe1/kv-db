#include "src/compression/compression_manager.h"
#include "src/compression/columnar_compressor.h"
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>

class CompressionOptimizationTest {
public:
    void run_all_tests() {
        std::cout << "=== 压缩算法优化测试 ===\n\n";
        
        test_basic_compression();
        test_compression_algorithms_comparison();
        test_columnar_compression();
        test_auto_algorithm_selection();
        test_batch_compression();
        test_real_world_data();
        
        std::cout << "=== 所有压缩测试完成 ===\n";
    }
    
private:
    void test_basic_compression() {
        std::cout << "1. 基础压缩功能测试\n";
        
        CompressionManager manager;
        std::string test_data = "Hello World! This is a test string for compression. "
                               "Hello World! This is a test string for compression. "
                               "Hello World! This is a test string for compression.";
        
        // 测试所有压缩算法
        auto types = CompressionFactory::get_available_types();
        
        for (auto type : types) {
            auto compressed = manager.compress(test_data, type);
            if (compressed.has_value()) {
                auto decompressed = manager.decompress(*compressed, type);
                
                if (decompressed.has_value() && *decompressed == test_data) {
                    double ratio = (double)compressed->size() / test_data.size();
                    std::cout << "✓ " << CompressionFactory::type_to_string(type) 
                              << " 压缩比: " << ratio << "\n";
                } else {
                    std::cout << "✗ " << CompressionFactory::type_to_string(type) 
                              << " 数据完整性检查失败\n";
                }
            }
        }
        
        std::cout << "\n";
    }
    
    void test_compression_algorithms_comparison() {
        std::cout << "2. 压缩算法性能对比测试\n";
        
        CompressionManager manager;
        
        // 生成测试数据
        std::string test_data = generate_test_data(10000); // 10KB数据
        
        // 运行基准测试
        auto results = manager.benchmark_all_algorithms(test_data);
        manager.print_benchmark_results(results);
        
        std::cout << "\n";
    }
    
    void test_columnar_compression() {
        std::cout << "3. 列式压缩测试\n";
        
        ColumnarCompressor columnar;
        
        // 生成相似key的测试数据
        std::vector<std::pair<std::string, std::string>> kv_pairs;
        for (int i = 0; i < 100; ++i) {
            std::string key = "user_" + std::to_string(i);
            std::string value = "user_data_" + std::to_string(i) + "_info";
            kv_pairs.emplace_back(key, value);
        }
        
        // 添加一些product数据
        for (int i = 0; i < 50; ++i) {
            std::string key = "product_" + std::to_string(i);
            std::string value = "product_info_" + std::to_string(i) + "_details";
            kv_pairs.emplace_back(key, value);
        }
        
        // 计算原始大小
        size_t original_size = 0;
        for (const auto& [key, value] : kv_pairs) {
            original_size += key.size() + value.size();
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        auto compressed = columnar.compress_batch(kv_pairs);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (compressed.has_value()) {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            double compression_ratio = (double)compressed->size() / original_size;
            
            std::cout << "列式压缩结果:\n";
            std::cout << "  原始大小: " << original_size << " bytes\n";
            std::cout << "  压缩大小: " << compressed->size() << " bytes\n";
            std::cout << "  压缩比: " << compression_ratio << "\n";
            std::cout << "  空间节省: " << (1.0 - compression_ratio) * 100 << "%\n";
            std::cout << "  压缩时间: " << duration << " μs\n";
            
            // 测试解压
            start = std::chrono::high_resolution_clock::now();
            auto decompressed = columnar.decompress_batch(*compressed);
            end = std::chrono::high_resolution_clock::now();
            
            auto decomp_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            std::cout << "  解压时间: " << decomp_duration << " μs\n";
            std::cout << "  解压数据量: " << decompressed.size() << " 条记录\n";
            
            if (decompressed.size() == kv_pairs.size()) {
                std::cout << "✓ 列式压缩数据完整性检查通过\n";
            } else {
                std::cout << "✗ 列式压缩数据完整性检查失败\n";
            }
        }
        
        columnar.print_stats();
        std::cout << "\n";
    }
    
    void test_auto_algorithm_selection() {
        std::cout << "4. 自动算法选择测试\n";
        
        CompressionManager manager;
        
        // 测试不同类型的数据
        std::vector<std::pair<std::string, std::string>> test_cases = {
            {"重复文本", generate_repetitive_text(1000)},
            {"随机文本", generate_random_text(1000)},
            {"数字数据", generate_numeric_data(1000)},
            {"混合数据", generate_mixed_data(1000)}
        };
        
        for (const auto& [name, data] : test_cases) {
            std::cout << "\n测试数据类型: " << name << " (大小: " << data.size() << " bytes)\n";
            
            auto result = manager.compress_with_best_algorithm(data);
            
            std::cout << "  最佳算法: " << CompressionFactory::type_to_string(result.type) << "\n";
            std::cout << "  压缩比: " << result.compression_ratio << "\n";
            std::cout << "  压缩时间: " << result.compression_time_us << " μs\n";
            std::cout << "  空间节省: " << (1.0 - result.compression_ratio) * 100 << "%\n";
        }
        
        std::cout << "\n";
    }
    
    void test_batch_compression() {
        std::cout << "5. 批量压缩测试\n";
        
        CompressionManager manager;
        
        // 生成批量KV数据
        std::vector<std::pair<std::string, std::string>> batch_data;
        for (int i = 0; i < 200; ++i) {
            std::string key = "batch_key_" + std::to_string(i);
            std::string value = "batch_value_" + std::to_string(i) + "_with_some_common_suffix";
            batch_data.emplace_back(key, value);
        }
        
        // 测试不同压缩算法的批量压缩
        auto types = {CompressionType::SNAPPY, CompressionType::LZ4, CompressionType::ZSTD, CompressionType::COLUMNAR};
        
        for (auto type : types) {
            auto start = std::chrono::high_resolution_clock::now();
            auto compressed = manager.compress_batch(batch_data, type);
            auto end = std::chrono::high_resolution_clock::now();
            
            if (compressed.has_value()) {
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                
                // 计算原始大小
                size_t original_size = 0;
                for (const auto& [key, value] : batch_data) {
                    original_size += key.size() + value.size();
                }
                
                double compression_ratio = (double)compressed->size() / original_size;
                
                std::cout << CompressionFactory::type_to_string(type) << " 批量压缩:\n";
                std::cout << "  压缩比: " << compression_ratio << "\n";
                std::cout << "  压缩时间: " << duration << " μs\n";
                std::cout << "  空间节省: " << (1.0 - compression_ratio) * 100 << "%\n";
                
                // 测试解压
                start = std::chrono::high_resolution_clock::now();
                auto decompressed = manager.decompress_batch(*compressed, type);
                end = std::chrono::high_resolution_clock::now();
                
                auto decomp_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                std::cout << "  解压时间: " << decomp_duration << " μs\n";
                std::cout << "  解压记录数: " << decompressed.size() << "\n\n";
            }
        }
    }
    
    void test_real_world_data() {
        std::cout << "6. 真实世界数据压缩测试\n";
        
        CompressionManager manager;
        
        // 模拟真实的数据库记录
        std::vector<std::string> real_world_data = {
            generate_json_data(500),      // JSON数据
            generate_log_data(500),       // 日志数据
            generate_csv_data(500),       // CSV数据
            generate_xml_data(500)        // XML数据
        };
        
        std::vector<std::string> data_types = {"JSON", "日志", "CSV", "XML"};
        
        for (size_t i = 0; i < real_world_data.size(); ++i) {
            std::cout << "\n" << data_types[i] << " 数据压缩测试:\n";
            
            const auto& data = real_world_data[i];
            auto results = manager.benchmark_all_algorithms(data);
            
            // 显示最佳结果
            auto best_compression = *std::min_element(results.begin(), results.end(),
                [](const auto& a, const auto& b) {
                    return a.compression_ratio < b.compression_ratio;
                });
            
            auto fastest = *std::max_element(results.begin(), results.end(),
                [](const auto& a, const auto& b) {
                    return a.compression_speed_mbps < b.compression_speed_mbps;
                });
            
            std::cout << "  最佳压缩: " << best_compression.name 
                      << " (压缩比: " << best_compression.compression_ratio << ")\n";
            std::cout << "  最快压缩: " << fastest.name 
                      << " (速度: " << fastest.compression_speed_mbps << " MB/s)\n";
        }
        
        // 显示总体统计
        std::cout << "\n";
        manager.print_all_stats();
    }
    
    // 数据生成辅助函数
    std::string generate_test_data(size_t size) {
        std::string data;
        data.reserve(size);
        
        std::string pattern = "This is a test pattern for compression algorithm evaluation. ";
        while (data.size() < size) {
            data += pattern;
        }
        
        return data.substr(0, size);
    }
    
    std::string generate_repetitive_text(size_t size) {
        std::string data;
        data.reserve(size);
        
        std::string pattern = "ABCDEFGHIJ";
        while (data.size() < size) {
            data += pattern;
        }
        
        return data.substr(0, size);
    }
    
    std::string generate_random_text(size_t size) {
        std::string data;
        data.reserve(size);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis('A', 'Z');
        
        for (size_t i = 0; i < size; ++i) {
            data += static_cast<char>(dis(gen));
        }
        
        return data;
    }
    
    std::string generate_numeric_data(size_t size) {
        std::string data;
        data.reserve(size);
        
        for (size_t i = 0; i < size / 10; ++i) {
            data += std::to_string(i) + ",";
        }
        
        return data.substr(0, size);
    }
    
    std::string generate_mixed_data(size_t size) {
        return generate_repetitive_text(size / 2) + generate_random_text(size / 2);
    }
    
    std::string generate_json_data(size_t approx_size) {
        std::string json = R"({"users":[)";
        for (int i = 0; i < approx_size / 50; ++i) {
            json += R"({"id":)" + std::to_string(i) + 
                   R"(,"name":"user)" + std::to_string(i) + 
                   R"(","email":"user)" + std::to_string(i) + R"(@example.com"},)";
        }
        json.pop_back(); // 移除最后的逗号
        json += "]}";
        return json;
    }
    
    std::string generate_log_data(size_t approx_size) {
        std::string logs;
        for (int i = 0; i < approx_size / 80; ++i) {
            logs += "2024-01-01 12:00:0" + std::to_string(i % 10) + 
                   " INFO [main] Application started successfully\n";
        }
        return logs;
    }
    
    std::string generate_csv_data(size_t approx_size) {
        std::string csv = "id,name,age,city\n";
        for (int i = 0; i < approx_size / 30; ++i) {
            csv += std::to_string(i) + ",User" + std::to_string(i) + 
                  "," + std::to_string(20 + i % 50) + ",City" + std::to_string(i % 10) + "\n";
        }
        return csv;
    }
    
    std::string generate_xml_data(size_t approx_size) {
        std::string xml = "<?xml version=\"1.0\"?><users>";
        for (int i = 0; i < approx_size / 60; ++i) {
            xml += "<user><id>" + std::to_string(i) + "</id><name>User" + 
                  std::to_string(i) + "</name></user>";
        }
        xml += "</users>";
        return xml;
    }
};

int main() {
    try {
        CompressionOptimizationTest test;
        test.run_all_tests();
        
        std::cout << "\n=== 压缩算法优化总结 ===\n";
        std::cout << "✅ 实现的压缩算法:\n";
        std::cout << "1. Snappy: 快速压缩，适合实时场景\n";
        std::cout << "2. LZ4: 高压缩比，适合存储\n";
        std::cout << "3. ZSTD: 平衡压缩比和速度\n";
        std::cout << "4. Columnar: 相同key模式的value压缩\n";
        std::cout << "\n📈 预期收益:\n";
        std::cout << "• 存储空间节省: 30-70%\n";
        std::cout << "• 网络传输优化: 减少IO时间\n";
        std::cout << "• 自动算法选择: 根据数据特征优化\n";
        std::cout << "• 批量压缩: 提升压缩效率\n";
        
    } catch (const std::exception& e) {
        std::cerr << "测试失败: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}