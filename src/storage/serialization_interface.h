#pragma once
#include "data_types.h"
#include <string>
#include <memory>
#include <vector>
#include <functional>

namespace kvdb {

// 序列化格式枚举
enum class SerializationFormat {
    BINARY,         // 原生二进制格式（默认）
    JSON,           // JSON 格式
    PROTOBUF,       // Protocol Buffers
    MESSAGEPACK,    // MessagePack
    CUSTOM          // 用户自定义格式
};

// 序列化接口
class SerializationInterface {
public:
    virtual ~SerializationInterface() = default;
    
    // 序列化单个值
    virtual std::string serialize(const TypedValue& value) = 0;
    
    // 反序列化单个值
    virtual TypedValue deserialize(const std::string& data) = 0;
    
    // 批量序列化
    virtual std::string serialize_batch(const std::vector<TypedValue>& values) = 0;
    
    // 批量反序列化
    virtual std::vector<TypedValue> deserialize_batch(const std::string& data) = 0;
    
    // 获取格式类型
    virtual SerializationFormat get_format() const = 0;
    
    // 获取格式名称
    virtual std::string get_format_name() const = 0;
    
    // 估算序列化后的大小
    virtual size_t estimate_size(const TypedValue& value) const = 0;
    
    // 检查是否支持压缩
    virtual bool supports_compression() const { return false; }
    
    // 设置压缩选项
    virtual void set_compression(bool enable) {}
};

// 序列化工厂
class SerializationFactory {
public:
    // 创建序列化器
    static std::unique_ptr<SerializationInterface> create_serializer(SerializationFormat format);
    
    // 注册自定义序列化器
    static void register_custom_serializer(const std::string& name, 
                                         std::function<std::unique_ptr<SerializationInterface>()> factory);
    
    // 获取所有支持的格式
    static std::vector<SerializationFormat> get_supported_formats();
    
    // 格式名称转换
    static std::string format_to_string(SerializationFormat format);
    static SerializationFormat string_to_format(const std::string& format_str);
};

// 序列化配置
struct SerializationConfig {
    SerializationFormat format = SerializationFormat::BINARY;
    bool enable_compression = false;
    int compression_level = 6;  // 1-9, 6为默认
    bool pretty_print = false;  // 仅对JSON等文本格式有效
    
    SerializationConfig() = default;
    SerializationConfig(SerializationFormat fmt) : format(fmt) {}
};

// 序列化管理器
class SerializationManager {
private:
    std::unique_ptr<SerializationInterface> serializer_;
    SerializationConfig config_;
    
public:
    explicit SerializationManager(const SerializationConfig& config = SerializationConfig());
    
    // 设置配置
    void set_config(const SerializationConfig& config);
    const SerializationConfig& get_config() const { return config_; }
    
    // 序列化操作
    std::string serialize(const TypedValue& value);
    TypedValue deserialize(const std::string& data);
    
    std::string serialize_batch(const std::vector<TypedValue>& values);
    std::vector<TypedValue> deserialize_batch(const std::string& data);
    
    // 性能统计
    struct Stats {
        size_t serializations = 0;
        size_t deserializations = 0;
        size_t total_bytes_serialized = 0;
        size_t total_bytes_deserialized = 0;
        double avg_serialization_time = 0.0;
        double avg_deserialization_time = 0.0;
    };
    
    const Stats& get_stats() const { return stats_; }
    void reset_stats() { stats_ = Stats{}; }
    
private:
    Stats stats_;
    void update_serialization_stats(size_t bytes, double time);
    void update_deserialization_stats(size_t bytes, double time);
};

} // namespace kvdb