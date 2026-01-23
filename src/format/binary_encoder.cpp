#include "binary_encoder.h"
#include <iostream>
#include <cstring>

// 标准二进制编码器实现
class StandardBinaryEncoder : public BinaryEncoder {
public:
    StandardBinaryEncoder() {
        buffer_.reserve(1024); // 预分配1KB
    }
    
    void encode_varint(uint64_t value) override {
        // Varint编码：每个字节的最高位表示是否还有后续字节
        while (value >= 0x80) {
            buffer_.push_back(static_cast<uint8_t>(value | 0x80));
            value >>= 7;
        }
        buffer_.push_back(static_cast<uint8_t>(value));
    }
    
    void encode_fixed32(uint32_t value) override {
        // 小端序编码32位整数
        buffer_.push_back(static_cast<uint8_t>(value));
        buffer_.push_back(static_cast<uint8_t>(value >> 8));
        buffer_.push_back(static_cast<uint8_t>(value >> 16));
        buffer_.push_back(static_cast<uint8_t>(value >> 24));
    }
    
    void encode_fixed64(uint64_t value) override {
        // 小端序编码64位整数
        for (int i = 0; i < 8; ++i) {
            buffer_.push_back(static_cast<uint8_t>(value >> (i * 8)));
        }
    }
    
    void encode_string(const std::string& value) override {
        // 先编码长度，再编码内容
        encode_varint(value.size());
        buffer_.insert(buffer_.end(), value.begin(), value.end());
    }
    
    void encode_bytes(const std::vector<uint8_t>& value) override {
        // 先编码长度，再编码内容
        encode_varint(value.size());
        buffer_.insert(buffer_.end(), value.begin(), value.end());
    }
    
    void encode_bool(bool value) override {
        buffer_.push_back(value ? 1 : 0);
    }
    
    void encode_float(float value) override {
        uint32_t int_value;
        std::memcpy(&int_value, &value, sizeof(float));
        encode_fixed32(int_value);
    }
    
    void encode_double(double value) override {
        uint64_t int_value;
        std::memcpy(&int_value, &value, sizeof(double));
        encode_fixed64(int_value);
    }
    
    std::vector<uint8_t> get_encoded_data() const override {
        return buffer_;
    }
    
    void clear() override {
        buffer_.clear();
    }
    
    size_t size() const override {
        return buffer_.size();
    }
    
private:
    std::vector<uint8_t> buffer_;
};

// 标准二进制解码器实现
class StandardBinaryDecoder : public BinaryDecoder {
public:
    StandardBinaryDecoder() : data_(nullptr), size_(0), position_(0) {}
    
    void set_data(const std::vector<uint8_t>& data) override {
        data_ = data.data();
        size_ = data.size();
        position_ = 0;
    }
    
    void set_data(const uint8_t* data, size_t size) override {
        data_ = data;
        size_ = size;
        position_ = 0;
    }
    
    std::optional<uint64_t> decode_varint() override {
        if (!has_more_data()) return std::nullopt;
        
        uint64_t result = 0;
        int shift = 0;
        
        while (position_ < size_) {
            uint8_t byte = data_[position_++];
            result |= static_cast<uint64_t>(byte & 0x7F) << shift;
            
            if ((byte & 0x80) == 0) {
                return result;
            }
            
            shift += 7;
            if (shift >= 64) {
                // 防止溢出
                return std::nullopt;
            }
        }
        
        return std::nullopt; // 数据不完整
    }
    
    std::optional<uint32_t> decode_fixed32() override {
        if (remaining_bytes() < 4) return std::nullopt;
        
        uint32_t result = 0;
        for (int i = 0; i < 4; ++i) {
            result |= static_cast<uint32_t>(data_[position_++]) << (i * 8);
        }
        
        return result;
    }
    
    std::optional<uint64_t> decode_fixed64() override {
        if (remaining_bytes() < 8) return std::nullopt;
        
        uint64_t result = 0;
        for (int i = 0; i < 8; ++i) {
            result |= static_cast<uint64_t>(data_[position_++]) << (i * 8);
        }
        
        return result;
    }
    
    std::optional<std::string> decode_string() override {
        auto length_opt = decode_varint();
        if (!length_opt.has_value()) return std::nullopt;
        
        size_t length = static_cast<size_t>(length_opt.value());
        if (remaining_bytes() < length) return std::nullopt;
        
        std::string result(reinterpret_cast<const char*>(data_ + position_), length);
        position_ += length;
        
        return result;
    }
    
    std::optional<std::vector<uint8_t>> decode_bytes() override {
        auto length_opt = decode_varint();
        if (!length_opt.has_value()) return std::nullopt;
        
        size_t length = static_cast<size_t>(length_opt.value());
        if (remaining_bytes() < length) return std::nullopt;
        
        std::vector<uint8_t> result(data_ + position_, data_ + position_ + length);
        position_ += length;
        
        return result;
    }
    
    std::optional<bool> decode_bool() override {
        if (!has_more_data()) return std::nullopt;
        
        return data_[position_++] != 0;
    }
    
    std::optional<float> decode_float() override {
        auto int_value_opt = decode_fixed32();
        if (!int_value_opt.has_value()) return std::nullopt;
        
        float result;
        uint32_t int_value = int_value_opt.value();
        std::memcpy(&result, &int_value, sizeof(float));
        
        return result;
    }
    
    std::optional<double> decode_double() override {
        auto int_value_opt = decode_fixed64();
        if (!int_value_opt.has_value()) return std::nullopt;
        
        double result;
        uint64_t int_value = int_value_opt.value();
        std::memcpy(&result, &int_value, sizeof(double));
        
        return result;
    }
    
    bool has_more_data() const override {
        return position_ < size_;
    }
    
    size_t remaining_bytes() const override {
        return position_ < size_ ? size_ - position_ : 0;
    }
    
    void reset() override {
        position_ = 0;
    }
    
private:
    const uint8_t* data_;
    size_t size_;
    size_t position_;
};

// 工厂方法实现
std::unique_ptr<BinaryEncoder> BinaryEncoderFactory::create_encoder() {
    return std::make_unique<StandardBinaryEncoder>();
}

std::unique_ptr<BinaryDecoder> BinaryEncoderFactory::create_decoder() {
    return std::make_unique<StandardBinaryDecoder>();
}