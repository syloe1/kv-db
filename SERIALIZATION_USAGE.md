# KVDB 序列化功能使用指南

## 快速开始

### 1. 编译要求

```bash
# 基础依赖
sudo apt-get install build-essential cmake zlib1g-dev

# 可选依赖（用于完整功能）
sudo apt-get install nlohmann-json3-dev libmsgpack-dev
```

### 2. 编译测试

```bash
# 运行基础序列化测试
./test_serialization_basic.sh

# 运行使用示例
./build_serialization_example.sh
```

## 基本使用

### 1. 包含头文件

```cpp
#include "src/storage/data_types.h"
#include "src/storage/binary_serializer.h"
#include "src/storage/json_serializer.h"
```

### 2. 创建数据

```cpp
using namespace kvdb;

// 基本类型
TypedValue int_val(42);
TypedValue str_val("Hello");
TypedValue date_val(Date(2024, 1, 15));

// 集合类型
List list = {TypedValue(1), TypedValue(2), TypedValue(3)};
TypedValue list_val(list);

Map map;
map["name"] = TypedValue("Alice");
map["age"] = TypedValue(30);
TypedValue map_val(map);
```

### 3. Binary 序列化

```cpp
BinarySerializer serializer;

// 序列化单个值
std::string data = serializer.serialize(int_val);
TypedValue restored = serializer.deserialize(data);

// 批量序列化
std::vector<TypedValue> values = {int_val, str_val, date_val};
std::string batch_data = serializer.serialize_batch(values);
std::vector<TypedValue> restored_batch = serializer.deserialize_batch(batch_data);

// 启用压缩
serializer.set_compression(true);
std::string compressed = serializer.serialize_batch(values);
```

### 4. JSON 序列化

```cpp
JsonSerializer json_serializer(true);  // 启用美化输出

// 序列化为 JSON
std::string json_str = json_serializer.serialize(map_val);
std::cout << json_str << std::endl;

// 从 JSON 反序列化
TypedValue from_json = json_serializer.deserialize(json_str);

// 批量 JSON 操作
std::string json_batch = json_serializer.serialize_batch(values);
std::vector<TypedValue> from_json_batch = json_serializer.deserialize_batch(json_batch);
```

## 高级用法

### 1. 序列化管理器

```cpp
#include "src/storage/serialization_factory.cpp"

// 创建配置
SerializationConfig config(SerializationFormat::BINARY);
config.enable_compression = true;

// 创建管理器
SerializationManager manager(config);

// 使用管理器
std::string data = manager.serialize(value);
TypedValue restored = manager.deserialize(data);

// 获取统计信息
const auto& stats = manager.get_stats();
std::cout << "序列化次数: " << stats.serializations << std::endl;
std::cout << "平均时间: " << stats.avg_serialization_time << "ms" << std::endl;
```

### 2. 格式切换

```cpp
SerializationManager manager;

// 切换到 JSON 格式
SerializationConfig json_config(SerializationFormat::JSON);
json_config.pretty_print = true;
manager.set_config(json_config);

// 切换到压缩的 Binary 格式
SerializationConfig binary_config(SerializationFormat::BINARY);
binary_config.enable_compression = true;
manager.set_config(binary_config);
```

### 3. 性能优化

```cpp
BinarySerializer serializer;

// 对于大数据启用压缩
serializer.set_compression(true);

// 批量操作比单个操作更高效
std::vector<TypedValue> large_dataset;
// ... 填充数据
std::string batch_result = serializer.serialize_batch(large_dataset);

// 预估序列化大小
size_t estimated_size = serializer.estimate_size(value);
```

## 支持的数据类型

| 类型 | 描述 | Binary支持 | JSON支持 |
|------|------|------------|----------|
| NULL_TYPE | 空值 | ✅ | ✅ |
| INT | 64位整数 | ✅ | ✅ |
| FLOAT | 32位浮点数 | ✅ | ✅ |
| DOUBLE | 64位浮点数 | ✅ | ✅ |
| STRING | 字符串 | ✅ | ✅ |
| TIMESTAMP | 时间戳 | ✅ | ✅ |
| DATE | 日期 | ✅ | ✅ |
| LIST | 列表 | ✅ | ✅ |
| SET | 集合 | ✅ | ✅ |
| MAP | 映射 | ✅ | ✅ |
| BLOB | 二进制数据 | ✅ | ✅ (Base64) |

## 性能特征

### Binary 格式
- **优点**: 最快速度，最小体积，支持压缩
- **缺点**: 不可读，平台相关
- **适用**: 高性能存储，内部数据交换

### JSON 格式
- **优点**: 人类可读，跨平台，标准格式
- **缺点**: 体积较大，解析较慢
- **适用**: 配置文件，API 接口，调试

### 性能数据（3000个值）
```
格式      大小     序列化时间   反序列化时间
Binary    48.7KB   0.83ms      0.96ms
JSON      99.6KB   3.10ms      4.74ms
Binary+压缩 ~6KB   ~2.5ms      ~1.2ms
```

## 错误处理

```cpp
try {
    BinarySerializer serializer;
    std::string data = serializer.serialize(value);
    TypedValue restored = serializer.deserialize(data);
} catch (const std::exception& e) {
    std::cerr << "序列化错误: " << e.what() << std::endl;
}
```

## 最佳实践

### 1. 选择合适的格式
```cpp
// 高性能存储
SerializationConfig storage_config(SerializationFormat::BINARY);
storage_config.enable_compression = true;

// 配置文件
SerializationConfig config_format(SerializationFormat::JSON);
config_format.pretty_print = true;

// 网络传输
SerializationConfig network_config(SerializationFormat::BINARY);
network_config.enable_compression = true;
```

### 2. 批量操作
```cpp
// 好的做法：批量序列化
std::vector<TypedValue> values;
// ... 添加多个值
std::string batch_data = serializer.serialize_batch(values);

// 避免：多次单个序列化
// for (const auto& value : values) {
//     std::string data = serializer.serialize(value);  // 效率低
// }
```

### 3. 压缩策略
```cpp
// 根据数据大小决定是否压缩
if (estimated_size > 1024) {  // 大于 1KB
    serializer.set_compression(true);
} else {
    serializer.set_compression(false);  // 小数据不压缩
}
```

### 4. 错误恢复
```cpp
bool try_deserialize(const std::string& data, TypedValue& result) {
    try {
        BinarySerializer binary_serializer;
        result = binary_serializer.deserialize(data);
        return true;
    } catch (...) {
        try {
            JsonSerializer json_serializer;
            result = json_serializer.deserialize(data);
            return true;
        } catch (...) {
            return false;
        }
    }
}
```

## 扩展开发

### 1. 自定义序列化器
```cpp
class MyCustomSerializer : public SerializationInterface {
public:
    std::string serialize(const TypedValue& value) override {
        // 自定义序列化逻辑
    }
    
    TypedValue deserialize(const std::string& data) override {
        // 自定义反序列化逻辑
    }
    
    SerializationFormat get_format() const override {
        return SerializationFormat::CUSTOM;
    }
    
    std::string get_format_name() const override {
        return "MyCustomFormat";
    }
    
    size_t estimate_size(const TypedValue& value) const override {
        // 估算序列化后的大小
    }
};
```

### 2. 注册自定义序列化器
```cpp
SerializationFactory::register_custom_serializer("my_format", 
    []() { return std::make_unique<MyCustomSerializer>(); });
```

## 故障排除

### 1. 编译错误
```bash
# 缺少 zlib
sudo apt-get install zlib1g-dev

# 缺少 C++17 支持
g++ -std=c++17 ...
```

### 2. 运行时错误
- **序列化失败**: 检查数据类型是否支持
- **反序列化失败**: 确保数据格式正确，版本兼容
- **压缩错误**: 检查 zlib 是否正确安装

### 3. 性能问题
- 使用批量操作而不是单个操作
- 对大数据启用压缩
- 选择合适的序列化格式
- 避免频繁的格式切换

## 更多示例

查看以下文件获取更多使用示例：
- `test_serialization_basic.cpp` - 基础功能测试
- `serialization_example.cpp` - 使用示例
- `SERIALIZATION_OPTIMIZATION_SUMMARY.md` - 详细技术文档