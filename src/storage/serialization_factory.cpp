#include "serialization_interface.h"
#include "binary_serializer.h"
#include "json_serializer.h"
#include "messagepack_serializer.h"
#include "protobuf_serializer.h"
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace kvdb {

// 静态成员变量
static std::unordered_map<std::string, std::function<std::unique_ptr<SerializationInterface>()>> custom_serializers;

std::unique_ptr<SerializationInterface> SerializationFactory::create_serializer(SerializationFormat format) {
    switch (format) {
        case SerializationFormat::BINARY:
            return std::make_unique<BinarySerializer>();
            
        case SerializationFormat::JSON:
            return std::make_unique<JsonSerializer>();
            
        case SerializationFormat::MESSAGEPACK:
            return std::make_unique<MessagePackSerializer>();
            
        case SerializationFormat::PROTOBUF:
            return std::make_unique<ProtobufSerializer>();
            
        case SerializationFormat::CUSTOM:
            throw std::runtime_error("Custom serializer must be created through register_custom_serializer");
    }
    
    throw std::runtime_error("Unknown serialization format");
}

void SerializationFactory::register_custom_serializer(const std::string& name, 
                                                     std::function<std::unique_ptr<SerializationInterface>()> factory) {
    custom_serializers[name] = factory;
}

std::vector<SerializationFormat> SerializationFactory::get_supported_formats() {
    return {
        SerializationFormat::BINARY,
        SerializationFormat::JSON,
        SerializationFormat::MESSAGEPACK,
        SerializationFormat::PROTOBUF,
        SerializationFormat::CUSTOM
    };
}

std::string SerializationFactory::format_to_string(SerializationFormat format) {
    switch (format) {
        case SerializationFormat::BINARY: return "binary";
        case SerializationFormat::JSON: return "json";
        case SerializationFormat::MESSAGEPACK: return "messagepack";
        case SerializationFormat::PROTOBUF: return "protobuf";
        case SerializationFormat::CUSTOM: return "custom";
    }
    return "unknown";
}

SerializationFormat SerializationFactory::string_to_format(const std::string& format_str) {
    std::string lower_str = format_str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
    
    if (lower_str == "binary") return SerializationFormat::BINARY;
    if (lower_str == "json") return SerializationFormat::JSON;
    if (lower_str == "messagepack" || lower_str == "msgpack") return SerializationFormat::MESSAGEPACK;
    if (lower_str == "protobuf" || lower_str == "pb") return SerializationFormat::PROTOBUF;
    if (lower_str == "custom") return SerializationFormat::CUSTOM;
    
    throw std::runtime_error("Unknown serialization format: " + format_str);
}

// SerializationManager 实现
SerializationManager::SerializationManager(const SerializationConfig& config) 
    : config_(config) {
    serializer_ = SerializationFactory::create_serializer(config_.format);
    
    // 设置序列化器选项
    if (serializer_->supports_compression()) {
        serializer_->set_compression(config_.enable_compression);
    }
    
    // 对于 JSON 序列化器，设置格式化选项
    if (config_.format == SerializationFormat::JSON) {
        auto json_serializer = dynamic_cast<JsonSerializer*>(serializer_.get());
        if (json_serializer) {
            json_serializer->set_pretty_print(config_.pretty_print);
        }
    }
}

void SerializationManager::set_config(const SerializationConfig& config) {
    if (config_.format != config.format) {
        // 格式改变，需要重新创建序列化器
        serializer_ = SerializationFactory::create_serializer(config.format);
    }
    
    config_ = config;
    
    // 更新序列化器选项
    if (serializer_->supports_compression()) {
        serializer_->set_compression(config_.enable_compression);
    }
    
    if (config_.format == SerializationFormat::JSON) {
        auto json_serializer = dynamic_cast<JsonSerializer*>(serializer_.get());
        if (json_serializer) {
            json_serializer->set_pretty_print(config_.pretty_print);
        }
    }
}

std::string SerializationManager::serialize(const TypedValue& value) {
    auto start = std::chrono::high_resolution_clock::now();
    
    std::string result = serializer_->serialize(value);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start).count();
    
    update_serialization_stats(result.size(), duration);
    
    return result;
}

TypedValue SerializationManager::deserialize(const std::string& data) {
    auto start = std::chrono::high_resolution_clock::now();
    
    TypedValue result = serializer_->deserialize(data);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start).count();
    
    update_deserialization_stats(data.size(), duration);
    
    return result;
}

std::string SerializationManager::serialize_batch(const std::vector<TypedValue>& values) {
    auto start = std::chrono::high_resolution_clock::now();
    
    std::string result = serializer_->serialize_batch(values);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start).count();
    
    update_serialization_stats(result.size(), duration);
    
    return result;
}

std::vector<TypedValue> SerializationManager::deserialize_batch(const std::string& data) {
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<TypedValue> result = serializer_->deserialize_batch(data);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start).count();
    
    update_deserialization_stats(data.size(), duration);
    
    return result;
}

void SerializationManager::update_serialization_stats(size_t bytes, double time) {
    stats_.serializations++;
    stats_.total_bytes_serialized += bytes;
    
    // 计算移动平均
    double alpha = 0.1;  // 平滑因子
    if (stats_.serializations == 1) {
        stats_.avg_serialization_time = time;
    } else {
        stats_.avg_serialization_time = alpha * time + (1 - alpha) * stats_.avg_serialization_time;
    }
}

void SerializationManager::update_deserialization_stats(size_t bytes, double time) {
    stats_.deserializations++;
    stats_.total_bytes_deserialized += bytes;
    
    // 计算移动平均
    double alpha = 0.1;  // 平滑因子
    if (stats_.deserializations == 1) {
        stats_.avg_deserialization_time = time;
    } else {
        stats_.avg_deserialization_time = alpha * time + (1 - alpha) * stats_.avg_deserialization_time;
    }
}

} // namespace kvdb