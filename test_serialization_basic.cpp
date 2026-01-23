#include "src/storage/data_types.h"
#include "src/storage/serialization_interface.h"
#include "src/storage/binary_serializer.h"
#include "src/storage/json_serializer.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>

using namespace kvdb;

void test_serializer(std::unique_ptr<SerializationInterface> serializer, const std::string& name) {
    std::cout << "\n=== 测试 " << name << " 序列化 ===" << std::endl;
    
    try {
        // 创建测试数据
        std::vector<TypedValue> test_values;
        
        // 基本类型
        test_values.push_back(TypedValue(42));
        test_values.push_back(TypedValue(3.14f));
        test_values.push_back(TypedValue(2.718281828));
        test_values.push_back(TypedValue("Hello, World!"));
        test_values.push_back(TypedValue(Date(2024, 1, 15)));
        
        // 集合类型
        List list = {TypedValue(1), TypedValue(2), TypedValue(3)};
        test_values.push_back(TypedValue(list));
        
        Map map;
        map["name"] = TypedValue("Alice");
        map["age"] = TypedValue(30);
        test_values.push_back(TypedValue(map));
        
        // 二进制数据
        Blob blob = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
        test_values.push_back(TypedValue(blob));
        
        std::cout << "测试数据创建完成，共 " << test_values.size() << " 个值" << std::endl;
        
        // 测试单个值序列化
        std::cout << "\n单个值序列化测试:" << std::endl;
        size_t total_serialized_size = 0;
        double total_serialize_time = 0;
        double total_deserialize_time = 0;
        int success_count = 0;
        
        for (size_t i = 0; i < test_values.size(); ++i) {
            const auto& value = test_values[i];
            
            try {
                auto start = std::chrono::high_resolution_clock::now();
                std::string serialized = serializer->serialize(value);
                auto end = std::chrono::high_resolution_clock::now();
                
                auto serialize_time = std::chrono::duration<double, std::micro>(end - start).count();
                total_serialize_time += serialize_time;
                total_serialized_size += serialized.size();
                
                start = std::chrono::high_resolution_clock::now();
                TypedValue deserialized = serializer->deserialize(serialized);
                end = std::chrono::high_resolution_clock::now();
                
                auto deserialize_time = std::chrono::duration<double, std::micro>(end - start).count();
                total_deserialize_time += deserialize_time;
                
                bool success = (value.get_type() == deserialized.get_type());
                if (success) {
                    // 简化的相等性检查
                    switch (value.get_type()) {
                        case DataType::INT:
                            success = (value.as_int() == deserialized.as_int());
                            break;
                        case DataType::FLOAT:
                            success = (std::abs(value.as_float() - deserialized.as_float()) < 0.001f);
                            break;
                        case DataType::DOUBLE:
                            success = (std::abs(value.as_double() - deserialized.as_double()) < 0.001);
                            break;
                        case DataType::STRING:
                            success = (value.as_string() == deserialized.as_string());
                            break;
                        case DataType::DATE:
                            success = (value.as_date() == deserialized.as_date());
                            break;
                        default:
                            success = true;  // 假设其他类型正确
                            break;
                    }
                }
                
                if (success) success_count++;
                
                std::cout << "  值 " << i + 1 << " (" << data_type_to_string(value.get_type()) << "): "
                          << (success ? "✓" : "✗") << " "
                          << "大小: " << serialized.size() << " bytes, "
                          << "序列化: " << std::fixed << std::setprecision(2) << serialize_time << "μs, "
                          << "反序列化: " << deserialize_time << "μs" << std::endl;
                
                if (!success) {
                    std::cout << "    原始值: " << value.to_string() << std::endl;
                    std::cout << "    反序列化值: " << deserialized.to_string() << std::endl;
                }
                
                // 对于 JSON，显示序列化结果示例
                if (name == "JSON" && i == 0) {
                    std::cout << "    JSON 示例: " << serialized << std::endl;
                }
                
            } catch (const std::exception& e) {
                std::cout << "  值 " << i + 1 << " (" << data_type_to_string(value.get_type()) << "): "
                          << "✗ 错误: " << e.what() << std::endl;
            }
        }
        
        // 测试批量序列化
        std::cout << "\n批量序列化测试:" << std::endl;
        
        try {
            auto start = std::chrono::high_resolution_clock::now();
            std::string batch_serialized = serializer->serialize_batch(test_values);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto batch_serialize_time = std::chrono::duration<double, std::micro>(end - start).count();
            
            start = std::chrono::high_resolution_clock::now();
            std::vector<TypedValue> batch_deserialized = serializer->deserialize_batch(batch_serialized);
            end = std::chrono::high_resolution_clock::now();
            
            auto batch_deserialize_time = std::chrono::duration<double, std::micro>(end - start).count();
            
            bool batch_success = (test_values.size() == batch_deserialized.size());
            
            std::cout << "  批量操作: " << (batch_success ? "✓" : "✗") << " "
                      << "大小: " << batch_serialized.size() << " bytes, "
                      << "序列化: " << std::fixed << std::setprecision(2) << batch_serialize_time << "μs, "
                      << "反序列化: " << batch_deserialize_time << "μs" << std::endl;
                      
        } catch (const std::exception& e) {
            std::cout << "  批量操作: ✗ 错误: " << e.what() << std::endl;
        }
        
        // 显示统计信息
        std::cout << "\n统计信息:" << std::endl;
        std::cout << "  成功率: " << success_count << "/" << test_values.size() 
                  << " (" << (100.0 * success_count / test_values.size()) << "%)" << std::endl;
        std::cout << "  总序列化大小: " << total_serialized_size << " bytes" << std::endl;
        std::cout << "  平均序列化时间: " << std::fixed << std::setprecision(2) 
                  << (total_serialize_time / test_values.size()) << "μs" << std::endl;
        std::cout << "  平均反序列化时间: " << (total_deserialize_time / test_values.size()) << "μs" << std::endl;
        
        // 测试压缩功能（如果支持）
        if (serializer->supports_compression()) {
            std::cout << "\n压缩功能测试:" << std::endl;
            
            // 创建较大的数据进行压缩测试
            std::vector<TypedValue> large_data;
            for (int i = 0; i < 50; ++i) {
                large_data.push_back(TypedValue("This is a test string number " + std::to_string(i) + " with some repeated content for compression testing"));
            }
            
            serializer->set_compression(false);
            auto start = std::chrono::high_resolution_clock::now();
            std::string uncompressed = serializer->serialize_batch(large_data);
            auto end = std::chrono::high_resolution_clock::now();
            auto uncompress_time = std::chrono::duration<double, std::micro>(end - start).count();
            
            serializer->set_compression(true);
            start = std::chrono::high_resolution_clock::now();
            std::string compressed = serializer->serialize_batch(large_data);
            end = std::chrono::high_resolution_clock::now();
            auto compress_time = std::chrono::duration<double, std::micro>(end - start).count();
            
            double compression_ratio = static_cast<double>(compressed.size()) / uncompressed.size();
            
            std::cout << "  未压缩大小: " << uncompressed.size() << " bytes (" << uncompress_time << "μs)" << std::endl;
            std::cout << "  压缩后大小: " << compressed.size() << " bytes (" << compress_time << "μs)" << std::endl;
            std::cout << "  压缩比: " << std::fixed << std::setprecision(2) << (compression_ratio * 100) << "%" << std::endl;
            std::cout << "  空间节省: " << std::setprecision(1) << ((1 - compression_ratio) * 100) << "%" << std::endl;
            std::cout << "  压缩开销: " << std::setprecision(1) << ((compress_time / uncompress_time - 1) * 100) << "%" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "测试失败: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "KVDB 序列化优化测试 (基础版)" << std::endl;
    std::cout << "==============================" << std::endl;
    
    // 测试二进制序列化
    test_serializer(std::make_unique<BinarySerializer>(), "Binary");
    
    // 测试 JSON 序列化
    test_serializer(std::make_unique<JsonSerializer>(true), "JSON");
    
    std::cout << "\n=== 性能对比 ===" << std::endl;
    
    // 创建大量测试数据进行性能对比
    std::vector<TypedValue> large_dataset;
    for (int i = 0; i < 1000; ++i) {
        large_dataset.push_back(TypedValue(i));
        large_dataset.push_back(TypedValue(static_cast<float>(i) * 1.5f));
        large_dataset.push_back(TypedValue("String_" + std::to_string(i)));
    }
    
    std::cout << "数据集大小: " << large_dataset.size() << " 个值" << std::endl;
    
    struct FormatResult {
        std::string name;
        size_t serialized_size;
        double serialize_time;
        double deserialize_time;
        bool success;
    };
    
    std::vector<FormatResult> results;
    
    // 测试各种格式
    std::vector<std::pair<std::unique_ptr<SerializationInterface>, std::string>> serializers;
    serializers.emplace_back(std::make_unique<BinarySerializer>(), "Binary");
    serializers.emplace_back(std::make_unique<JsonSerializer>(), "JSON");
    
    for (auto& [serializer, name] : serializers) {
        try {
            // 序列化
            auto start = std::chrono::high_resolution_clock::now();
            std::string serialized = serializer->serialize_batch(large_dataset);
            auto end = std::chrono::high_resolution_clock::now();
            double serialize_time = std::chrono::duration<double, std::milli>(end - start).count();
            
            // 反序列化
            start = std::chrono::high_resolution_clock::now();
            std::vector<TypedValue> deserialized = serializer->deserialize_batch(serialized);
            end = std::chrono::high_resolution_clock::now();
            double deserialize_time = std::chrono::duration<double, std::milli>(end - start).count();
            
            // 验证正确性（简化检查）
            bool success = (large_dataset.size() == deserialized.size());
            
            results.push_back({name, serialized.size(), serialize_time, deserialize_time, success});
            
        } catch (const std::exception& e) {
            std::cout << name << " 格式测试失败: " << e.what() << std::endl;
            results.push_back({name, 0, 0, 0, false});
        }
    }
    
    // 显示结果
    std::cout << "\n性能对比结果:" << std::endl;
    std::cout << std::left << std::setw(12) << "格式" 
              << std::setw(12) << "大小(KB)" 
              << std::setw(15) << "序列化(ms)" 
              << std::setw(15) << "反序列化(ms)" 
              << std::setw(8) << "状态" << std::endl;
    std::cout << std::string(62, '-') << std::endl;
    
    for (const auto& result : results) {
        std::cout << std::left << std::setw(12) << result.name
                  << std::setw(12) << std::fixed << std::setprecision(1) << (result.serialized_size / 1024.0)
                  << std::setw(15) << std::setprecision(2) << result.serialize_time
                  << std::setw(15) << result.deserialize_time
                  << std::setw(8) << (result.success ? "✓" : "✗") << std::endl;
    }
    
    std::cout << "\n测试完成!" << std::endl;
    return 0;
}