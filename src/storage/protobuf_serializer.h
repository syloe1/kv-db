#pragma once
#include "serialization_interface.h"

namespace kvdb {

// Protocol Buffers 序列化器
class ProtobufSerializer : public SerializationInterface {
public:
    ProtobufSerializer() = default;
    
    std::string serialize(const TypedValue& value) override;
    TypedValue deserialize(const std::string& data) override;
    
    std::string serialize_batch(const std::vector<TypedValue>& values) override;
    std::vector<TypedValue> deserialize_batch(const std::string& data) override;
    
    SerializationFormat get_format() const override { return SerializationFormat::PROTOBUF; }
    std::string get_format_name() const override { return "Protocol Buffers"; }
    
    size_t estimate_size(const TypedValue& value) const override;
    
    bool supports_compression() const override { return true; }
    void set_compression(bool enable) override { compression_enabled_ = enable; }
    
private:
    bool compression_enabled_ = false;
    
    // Varint 编码/解码
    void write_varint(std::ostream& os, uint64_t value) const;
    uint64_t read_varint(std::istream& is) const;
    
    // 压缩辅助函数
    std::string compress_data(const std::string& data) const;
    std::string decompress_data(const std::string& data) const;
};

} // namespace kvdb