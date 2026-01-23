#pragma once
#include "serialization_interface.h"
#include <stdexcept>
#ifdef HAVE_MSGPACK
#include <msgpack.hpp>
#endif

namespace kvdb {

#ifdef HAVE_MSGPACK
// MessagePack 序列化器
class MessagePackSerializer : public SerializationInterface {
public:
    MessagePackSerializer() = default;
    
    std::string serialize(const TypedValue& value) override;
    TypedValue deserialize(const std::string& data) override;
    
    std::string serialize_batch(const std::vector<TypedValue>& values) override;
    std::vector<TypedValue> deserialize_batch(const std::string& data) override;
    
    SerializationFormat get_format() const override { return SerializationFormat::MESSAGEPACK; }
    std::string get_format_name() const override { return "MessagePack"; }
    
    size_t estimate_size(const TypedValue& value) const override;
    
    bool supports_compression() const override { return true; }
    void set_compression(bool enable) override { compression_enabled_ = enable; }
    
private:
    bool compression_enabled_ = false;
    
    // TypedValue 到 MessagePack 的转换
    msgpack::object_handle typed_value_to_msgpack(const TypedValue& value, msgpack::zone& zone) const;
    TypedValue msgpack_to_typed_value(const msgpack::object& obj) const;
    
    // 压缩辅助函数
    std::string compress_data(const std::string& data) const;
    std::string decompress_data(const std::string& data) const;
};
#else
// MessagePack 不可用时的占位符
class MessagePackSerializer : public SerializationInterface {
public:
    MessagePackSerializer() = default;
    
    std::string serialize(const TypedValue& value) override {
        throw std::runtime_error("MessagePack serializer not available");
    }
    
    TypedValue deserialize(const std::string& data) override {
        throw std::runtime_error("MessagePack serializer not available");
    }
    
    std::string serialize_batch(const std::vector<TypedValue>& values) override {
        throw std::runtime_error("MessagePack serializer not available");
    }
    
    std::vector<TypedValue> deserialize_batch(const std::string& data) override {
        throw std::runtime_error("MessagePack serializer not available");
    }
    
    SerializationFormat get_format() const override { return SerializationFormat::MESSAGEPACK; }
    std::string get_format_name() const override { return "MessagePack (unavailable)"; }
    
    size_t estimate_size(const TypedValue& value) const override { return 0; }
    
    bool supports_compression() const override { return false; }
    void set_compression(bool enable) override {}
};
#endif

} // namespace kvdb