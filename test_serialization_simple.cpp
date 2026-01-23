#include "src/storage/data_types.h"
#include "src/storage/serialization_interface.h"
#include "src/storage/binary_serializer.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>

using namespace kvdb;

// 简化的序列化工厂实现
class SimpleSerializationFactory {
public:
    static std::unique_ptr<SerializationInterface> create_binary_serializer() {
        return std::make_unique<BinarySerializer>();
    }
};

void test_basic_serialization() {
    std::cout << "=== 基础序列化测试 ===" << std::endl;
    
    try {
        auto serializer = SimpleSerializationFactory::create_binary_serializer();
        
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
        for (size_t i = 0; i < test_values.size(); ++i) {
            const auto& value = test_values[i];
            
            auto start = std::chrono::high_resolution_clock::now();
            std::string serialized = serializer->serialize(value);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto serialize_time = std::chrono::duration<double, std::micro>(end - start).count();
            
            start = std::chrono::high_resolution_clock::now();
            TypedValue deserialized = serializer->deserialize(serialized);
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
        std::string batch_serialized = serializer->serialize_batch(test_values);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto batch_serialize_time = std::chrono::duration<double, std::micro>(end - start).count();
        
        start = std::chrono::high_resolution_clock::now();
        std::vector<TypedValue> batch_deserialized = serializer->deserialize_batch(batch_serialized);
        end = std::chrono::high_resolution_clock::now();
        
        auto batch_deserialize_time = std::chrono::duration<double, std::micro>(end - start).count();
        
        bool batch_success = (test_values.size() == batch_deserialized.size());
        if (batch_success) {
            for (size_t i = 0; i < test_values.size(); ++i) {
                if (!(test_values[i] == batch_deserialized[i])) {
                    batch_success = false;
                    std::cout << "    批量测试失败在索引 " << i << std::endl;
                    break;
                }
            }
        }
        
        std::cout << "  批量操作: " << (batch_success ? "✓" : "✗") << " "
                  << "大小: " << batch_serialized.size() << " bytes, "
                  << "序列化: " << std::fixed << std::setprecision(2) << batch_serialize_time << "μs, "
                  << "反序列化: " << batch_deserialize_time << "μs" << std::endl;
        
        // 测试压缩功能
        std::cout << "\n压缩功能测试:" << std::endl;
        serializer->set_compression(true);
        
        // 创建较大的数据进行压缩测试
        std::vector<TypedValue> large_data;
        for (int i = 0; i < 100; ++i) {
            large_data.push_back(TypedValue("This is a test string number " + std::to_string(i) + " with some repeated content"));
        }
        
        start = std::chrono::high_resolution_clock::now();
        std::string compressed = serializer->serialize_batch(large_data);
        end = std::chrono::high_resolution_clock::now();
        auto compress_time = std::chrono::duration<double, std::micro>(end - start).count();
        
        serializer->set_compression(false);
        start = std::chrono::high_resolution_clock::now();
        std::string uncompressed = serializer->serialize_batch(large_data);
        end = std::chrono::high_resolution_clock::now();
        auto uncompress_time = std::chrono::duration<double, std::micro>(end - start).count();
        
        double compression_ratio = static_cast<double>(compressed.size()) / uncompressed.size();
        
        std::cout << "  未压缩大小: " << uncompressed.size() << " bytes (" << uncompress_time << "μs)" << std::endl;
        std::cout << "  压缩后大小: " << compressed.size() << " bytes (" << compress_time << "μs)" << std::endl;
        std::cout << "  压缩比: " << std::fixed << std::setprecision(2) << (compression_ratio * 100) << "%" << std::endl;
        std::cout << "  空间节省: " << std::setprecision(1) << ((1 - compression_ratio) * 100) << "%" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "错误: " << e.what() << std::endl;
    }
}

void performance_test() {
    std::cout << "\n=== 性能测试 ===" << std::endl;
    
    try {
        auto serializer = SimpleSerializationFactory::create_binary_serializer();
        
        // 创建大量测试数据
        std::vector<TypedValue> large_dataset;
        
        std::cout << "生成测试数据..." << std::endl;
        for (int i = 0; i < 10000; ++i) {
            large_dataset.push_back(TypedValue(i));
            large_dataset.push_back(TypedValue(static_cast<float>(i) * 1.5f));
            large_dataset.push_back(TypedValue("String_" + std::to_string(i)));
            
            if (i % 100 == 0) {
                List list;
                for (int j = 0; j < 5; ++j) {
                    list.push_back(TypedValue(i + j));
                }
                large_dataset.push_back(TypedValue(list));
            }
        }
        
        std::cout << "数据集大小: " << large_dataset.size() << " 个值" << std::endl;
        
        // 测试不同配置的性能
        struct TestConfig {
            std::string name;
            bool compression;
        };
        
        std::vector<TestConfig> configs = {
            {"无压缩", false},
            {"启用压缩", true}
        };
        
        for (const auto& config : configs) {
            std::cout << "\n" << config.name << " 性能测试:" << std::endl;
            
            serializer->set_compression(config.compression);
            
            // 序列化性能测试
            auto start = std::chrono::high_resolution_clock::now();
            std::string serialized = serializer->serialize_batch(large_dataset);
            auto end = std::chrono::high_resolution_clock::now();
            double serialize_time = std::chrono::duration<double, std::milli>(end - start).count();
            
            // 反序列化性能测试
            start = std::chrono::high_resolution_clock::now();
            std::vector<TypedValue> deserialized = serializer->deserialize_batch(serialized);
            end = std::chrono::high_resolution_clock::now();
            double deserialize_time = std::chrono::duration<double, std::milli>(end - start).count();
            
            // 验证正确性
            bool success = (large_dataset.size() == deserialized.size());
            if (success) {
                for (size_t i = 0; i < std::min(large_dataset.size(), size_t(1000)); ++i) {
                    if (!(large_dataset[i] == deserialized[i])) {
                        success = false;
                        break;
                    }
                }
            }
            
            double throughput_serialize = (large_dataset.size() / serialize_time) * 1000;  // 值/秒
            double throughput_deserialize = (large_dataset.size() / deserialize_time) * 1000;  // 值/秒
            
            std::cout << "  序列化: " << std::fixed << std::setprecision(2) << serialize_time << "ms"
                      << " (" << std::setprecision(0) << throughput_serialize << " 值/秒)" << std::endl;
            std::cout << "  反序列化: " << std::setprecision(2) << deserialize_time << "ms"
                      << " (" << std::setprecision(0) << throughput_deserialize << " 值/秒)" << std::endl;
            std::cout << "  数据大小: " << (serialized.size() / 1024.0) << " KB" << std::endl;
            std::cout << "  正确性: " << (success ? "✓" : "✗") << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "性能测试错误: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "KVDB 序列化优化测试 (简化版)" << std::endl;
    std::cout << "==============================" << std::endl;
    
    test_basic_serialization();
    performance_test();
    
    std::cout << "\n测试完成!" << std::endl;
    return 0;
}