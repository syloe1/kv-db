#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <memory>

// 数据类型枚举
enum class DataType : uint8_t {
    UNKNOWN = 0,
    INT32 = 1,
    INT64 = 2,
    UINT32 = 3,
    UINT64 = 4,
    FLOAT = 5,
    DOUBLE = 6,
    STRING = 7,
    BYTES = 8,
    BOOL = 9,
    VARINT = 10,
    FIXED32 = 11,
    FIXED64 = 12
};

// 二进制编码器接口
class BinaryEncoder {
public:
    virtual ~BinaryEncoder() = default;
    
    // 基础类型编码
    virtual void encode_varint(uint64_t value) = 0;
    virtual void encode_fixed32(uint32_t value) = 0;
    virtual void encode_fixed64(uint64_t value) = 0;
    virtual void encode_string(const std::string& value) = 0;
    virtual void encode_bytes(const std::vector<uint8_t>& value) = 0;
    virtual void encode_bool(bool value) = 0;
    virtual void encode_float(float value) = 0;
    virtual void encode_double(double value) = 0;
    
    // 获取编码结果
    virtual std::vector<uint8_t> get_encoded_data() const = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
};

// 二进制解码器接口
class BinaryDecoder {
public:
    virtual ~BinaryDecoder() = default;
    
    // 设置要解码的数据
    virtual void set_data(const std::vector<uint8_t>& data) = 0;
    virtual void set_data(const uint8_t* data, size_t size) = 0;
    
    // 基础类型解码
    virtual std::optional<uint64_t> decode_varint() = 0;
    virtual std::optional<uint32_t> decode_fixed32() = 0;
    virtual std::optional<uint64_t> decode_fixed64() = 0;
    virtual std::optional<std::string> decode_string() = 0;
    virtual std::optional<std::vector<uint8_t>> decode_bytes() = 0;
    virtual std::optional<bool> decode_bool() = 0;
    virtual std::optional<float> decode_float() = 0;
    virtual std::optional<double> decode_double() = 0;
    
    // 状态查询
    virtual bool has_more_data() const = 0;
    virtual size_t remaining_bytes() const = 0;
    virtual void reset() = 0;
};

// 编码器工厂
class BinaryEncoderFactory {
public:
    static std::unique_ptr<BinaryEncoder> create_encoder();
    static std::unique_ptr<BinaryDecoder> create_decoder();
};