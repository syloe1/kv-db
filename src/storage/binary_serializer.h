#pragma once
#include "serialization_interface.h"

namespace kvdb {

// 二进制序列化器（原有的序列化方式）
class BinarySerializer : public SerializationInterface {
public:
    BinarySerializer() = default;
    
    std::string serialize(const TypedValue& value) override;
    TypedValue deserialize(const std::string& data) override;
    
    std::string serialize_batch(const std::vector<TypedValue>& values) override;
    std::vector<TypedValue> deserialize_batch(const std::string& data) override;
    
    SerializationFormat get_format() const override { return SerializationFormat::BINARY; }
    std::string get_format_name() const override { return "Binary"; }
    
    size_t estimate_size(const TypedValue& value) const override;
    
    bool supports_compression() const override { return true; }
    void set_compression(bool enable) override { compression_enabled_ = enable; }
    
private:
    bool compression_enabled_ = false;
    
    // 压缩和解压缩辅助函数
    std::string compress_data(const std::string& data) const;
    std::string decompress_data(const std::string& data) const;
};

} // namespace kvdb