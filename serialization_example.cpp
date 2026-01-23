#include "src/storage/data_types.h"
#include "src/storage/serialization_interface.h"
#include "src/storage/binary_serializer.h"
#include "src/storage/json_serializer.h"
#include <iostream>

using namespace kvdb;

int main() {
    std::cout << "KVDB 序列化使用示例" << std::endl;
    std::cout << "==================" << std::endl;
    
    // 创建测试数据
    TypedValue int_value(42);
    TypedValue string_value("Hello, KVDB!");
    TypedValue date_value(Date(2024, 1, 15));
    
    // 创建复合数据
    List list = {TypedValue(1), TypedValue(2), TypedValue(3)};
    TypedValue list_value(list);
    
    Map map;
    map["name"] = TypedValue("Alice");
    map["age"] = TypedValue(30);
    map["active"] = TypedValue(1);  // 作为布尔值
    TypedValue map_value(map);
    
    std::vector<TypedValue> test_data = {
        int_value, string_value, date_value, list_value, map_value
    };
    
    std::cout << "\n=== Binary 序列化示例 ===" << std::endl;
    {
        BinarySerializer binary_serializer;
        
        // 单个值序列化
        std::string serialized = binary_serializer.serialize(int_value);
        std::cout << "整数 42 序列化后大小: " << serialized.size() << " bytes" << std::endl;
        
        TypedValue deserialized = binary_serializer.deserialize(serialized);
        std::cout << "反序列化结果: " << deserialized.to_string() << std::endl;
        
        // 批量序列化
        std::string batch_serialized = binary_serializer.serialize_batch(test_data);
        std::cout << "批量序列化大小: " << batch_serialized.size() << " bytes" << std::endl;
        
        // 测试压缩
        binary_serializer.set_compression(true);
        std::string compressed = binary_serializer.serialize_batch(test_data);
        std::cout << "压缩后大小: " << compressed.size() << " bytes" << std::endl;
        std::cout << "压缩比: " << (100.0 * compressed.size() / batch_serialized.size()) << "%" << std::endl;
    }
    
    std::cout << "\n=== JSON 序列化示例 ===" << std::endl;
    {
        JsonSerializer json_serializer(true);  // 启用美化输出
        
        // 单个值序列化
        std::string json_str = json_serializer.serialize(map_value);
        std::cout << "Map 对象的 JSON 表示:" << std::endl;
        std::cout << json_str << std::endl;
        
        // 反序列化
        TypedValue json_deserialized = json_serializer.deserialize(json_str);
        std::cout << "反序列化结果: " << json_deserialized.to_string() << std::endl;
        
        // 批量序列化
        std::string json_batch = json_serializer.serialize_batch(test_data);
        std::cout << "\n批量 JSON 序列化大小: " << json_batch.size() << " bytes" << std::endl;
        
        // 显示部分 JSON 内容
        std::cout << "JSON 内容预览 (前 200 字符):" << std::endl;
        std::cout << json_batch.substr(0, 200) << "..." << std::endl;
    }
    
    std::cout << "\n=== 格式对比 ===" << std::endl;
    {
        BinarySerializer binary_serializer;
        JsonSerializer json_serializer;
        
        std::string binary_data = binary_serializer.serialize_batch(test_data);
        std::string json_data = json_serializer.serialize_batch(test_data);
        
        std::cout << "Binary 格式大小: " << binary_data.size() << " bytes" << std::endl;
        std::cout << "JSON 格式大小: " << json_data.size() << " bytes" << std::endl;
        std::cout << "JSON 相对于 Binary 的大小比: " 
                  << (100.0 * json_data.size() / binary_data.size()) << "%" << std::endl;
        
        // 启用压缩的 Binary
        binary_serializer.set_compression(true);
        std::string compressed_binary = binary_serializer.serialize_batch(test_data);
        std::cout << "Binary 压缩后大小: " << compressed_binary.size() << " bytes" << std::endl;
        std::cout << "压缩节省空间: " 
                  << (100.0 * (binary_data.size() - compressed_binary.size()) / binary_data.size()) 
                  << "%" << std::endl;
    }
    
    std::cout << "\n=== 使用建议 ===" << std::endl;
    std::cout << "1. 高性能存储: 使用 Binary + 压缩" << std::endl;
    std::cout << "2. 配置文件: 使用 JSON + 美化输出" << std::endl;
    std::cout << "3. 网络传输: 根据带宽选择 Binary 或 JSON" << std::endl;
    std::cout << "4. 调试和开发: 使用 JSON 便于查看数据" << std::endl;
    
    return 0;
}