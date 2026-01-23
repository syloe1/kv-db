#include "src/storage/serialization_interface.h"
#include "src/storage/serialization_factory.cpp"
#include "src/storage/binary_serializer.cpp"
#include "src/storage/json_serializer.cpp"
#include "src/storage/messagepack_serializer.cpp"
#include "src/storage/protobuf_serializer.cpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>

using namespace kvdb;

void test_serialization_format(SerializationFormat format, const std::string& format_name) {
    std::cout << "\n=== 测试 " << format_name << " 序列化 ===" << std::endl;
    
    try {
        SerializationConfig config(format);
        config.enable_compression = true;
        config.pretty_print = (format == SerializationFormat::JSON);
        
        SerializationManager manager(config);
        
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
        
        Set set = {TypedValue("apple"), TypedValue("banana"), TypedValue("cherry")};
        test_values.push_back(TypedValue(set));
        
        Map map;
        map["name"] = TypedValue("Alice");
        map["age"] = TypedValue(30);
        map["city"] = TypedValue("Beijing");
        test_values.push_back(TypedValue(map));
        
        // 二进制数据
        Blob blob = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
        test_values.push_back(TypedValue(blob));
        
        // 测试单个值序列化
        std::cout << "单个值序列化测试:" << std::endl;
        for (size_t i = 0; i < test_values.size(); ++i) {
            const auto& value = test_values[i];
            
            auto start = std::chrono::high_resolution_clock::now();
            std::string serialized = manager.serialize(value);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto serialize_time = std::chrono::duration<double, std::micro>(end - start).count();
            
            start = std::chrono::high_resolution_clock::now();
            TypedValue deserialized = manager.deserialize(serialized);
            end = std::chrono::high_resolution_clock::now();
            
            auto deserialize_time = std::chrono::duration<double, std::micro>(end - start).count();
            
            bool success = (value == deserialized);
            
            std::cout << "  值 " << i + 1 << " (" << data_type_to_string(value.get_type()) << "): "
                      << (success ? "✓" : "✗") << " "
                      << "大小: " << serialized.size() << " bytes, "
                      << "序列化: " << std::fixed << std::setprecision(2) << serialize_time << "μs, "
                      << "反序列化: " << deserialize_time << "μs" << std::endl;
            
            if (!success) {
                std::cout << "    原始值: " << value.to_string() << std::endl;
                std::cout << "    反序列化值: " << deserialized.to_string() << std::endl;
            }
        }
        
        // 测试批量序列化
        std::cout << "\n批量序列化测试:" << std::endl;
        
        auto start = std::chrono::high_resolution_clock::now();
        std::string batch_serialized = manager.serialize_batch(test_values);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto batch_serialize_time = std::chrono::duration<double, std::micro>(end - start).count();
        
        start = std::chrono::high_resolution_clock::now();
        std::vector<TypedValue> batch_deserialized = manager.deserialize_batch(batch_serialized);
        end = std::chrono::high_resolution_clock::now();
        
        auto batch_deserialize_time = std::chrono::duration<double, std::micro>(end - start).count();
        
        bool batch_success = (test_values.size() == batch_deserialized.size());
        if (batch_success) {
            for (size_t i = 0; i < test_values.size(); ++i) {
                if (!(test_values[i] == batch_deserialized[i])) {
                    batch_success = false;
                    break;
                }
            }
        }
        
        std::cout << "  批量操作: " << (batch_success ? "✓" : "✗") << " "
                  << "大小: " << batch_serialized.size() << " bytes, "
                  << "序列化: " << std::fixed << std::setprecision(2) << batch_serialize_time << "μs, "
                  << "反序列化: " << batch_deserialize_time << "μs" << std::endl;
        
        // 显示统计信息
        const auto& stats = manager.get_stats();
        std::cout << "\n统计信息:" << std::endl;
        std::cout << "  序列化次数: " << stats.serializations << std::endl;
        std::cout << "  反序列化次数: " << stats.deserializations << std::endl;
        std::cout << "  序列化字节数: " << stats.total_bytes_serialized << std::endl;
        std::cout << "  反序列化字节数: " << stats.total_bytes_deserialized << std::endl;
        std::cout << "  平均序列化时间: " << std::fixed << std::setprecision(2) 
                  << stats.avg_serialization_time << "ms" << std::endl;
        std::cout << "  平均反序列化时间: " << stats.avg_deserialization_time << "ms" << std::endl;
        
        // 对于 JSON 格式，显示序列化后的内容示例
        if (format == SerializationFormat::JSON && !test_values.empty()) {
            std::cout << "\nJSON 示例 (第一个值):" << std::endl;
            std::string json_example = manager.serialize(test_values[0]);
            std::cout << json_example << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "错误: " << e.what() << std::endl;
    }
}

void performance_comparison() {
    std::cout << "\n=== 性能对比测试 ===" << std::endl;
    
    // 创建大量测试数据
    std::vector<TypedValue> large_dataset;
    
    // 添加各种类型的数据
    for (int i = 0; i < 1000; ++i) {
        large_dataset.push_back(TypedValue(i));
        large_dataset.push_back(TypedValue(static_cast<float>(i) * 1.5f));
        large_dataset.push_back(TypedValue("String_" + std::to_string(i)));
        
        if (i % 10 == 0) {
            List list;
            for (int j = 0; j < 5; ++j) {
                list.push_back(TypedValue(i + j));
            }
            large_dataset.push_back(TypedValue(list));
        }
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
    std::vector<std::pair<SerializationFormat, std::string>> formats = {
        {SerializationFormat::BINARY, "Binary"},
        {SerializationFormat::JSON, "JSON"},
        {SerializationFormat::MESSAGEPACK, "MessagePack"},
        {SerializationFormat::PROTOBUF, "Protobuf"}
    };
    
    for (const auto& [format, name] : formats) {
        try {
            SerializationConfig config(format);
            config.enable_compression = true;
            SerializationManager manager(config);
            
            // 序列化
            auto start = std::chrono::high_resolution_clock::now();
            std::string serialized = manager.serialize_batch(large_dataset);
            auto end = std::chrono::high_resolution_clock::now();
            double serialize_time = std::chrono::duration<double, std::milli>(end - start).count();
            
            // 反序列化
            start = std::chrono::high_resolution_clock::now();
            std::vector<TypedValue> deserialized = manager.deserialize_batch(serialized);
            end = std::chrono::high_resolution_clock::now();
            double deserialize_time = std::chrono::duration<double, std::milli>(end - start).count();
            
            // 验证正确性
            bool success = (large_dataset.size() == deserialized.size());
            if (success) {
                for (size_t i = 0; i < std::min(large_dataset.size(), size_t(100)); ++i) {
                    if (!(large_dataset[i] == deserialized[i])) {
                        success = false;
                        break;
                    }
                }
            }
            
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
    
    // 找出最佳格式
    if (!results.empty()) {
        auto best_size = std::min_element(results.begin(), results.end(),
            [](const FormatResult& a, const FormatResult& b) {
                return a.success && (!b.success || a.serialized_size < b.serialized_size);
            });
        
        auto best_speed = std::min_element(results.begin(), results.end(),
            [](const FormatResult& a, const FormatResult& b) {
                return a.success && (!b.success || (a.serialize_time + a.deserialize_time) < (b.serialize_time + b.deserialize_time));
            });
        
        std::cout << "\n推荐:" << std::endl;
        if (best_size->success) {
            std::cout << "  最小存储空间: " << best_size->name << " (" << (best_size->serialized_size / 1024.0) << " KB)" << std::endl;
        }
        if (best_speed->success) {
            std::cout << "  最快速度: " << best_speed->name << " (" << (best_speed->serialize_time + best_speed->deserialize_time) << " ms)" << std::endl;
        }
    }
}

int main() {
    std::cout << "KVDB 序列化优化测试" << std::endl;
    std::cout << "===================" << std::endl;
    
    // 测试各种序列化格式
    test_serialization_format(SerializationFormat::BINARY, "Binary");
    test_serialization_format(SerializationFormat::JSON, "JSON");
    test_serialization_format(SerializationFormat::MESSAGEPACK, "MessagePack");
    test_serialization_format(SerializationFormat::PROTOBUF, "Protocol Buffers");
    
    // 性能对比
    performance_comparison();
    
    std::cout << "\n测试完成!" << std::endl;
    return 0;
}