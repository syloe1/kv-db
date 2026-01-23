#include "binary_serializer.h"
#include <sstream>
#include <zlib.h>
#include <cstring>

namespace kvdb {

std::string BinarySerializer::serialize(const TypedValue& value) {
    std::string data = value.serialize();
    
    if (compression_enabled_ && data.size() > 64) {  // 只对较大的数据进行压缩
        data = compress_data(data);
    }
    
    return data;
}

TypedValue BinarySerializer::deserialize(const std::string& data) {
    std::string actual_data = data;
    
    if (compression_enabled_) {
        try {
            actual_data = decompress_data(data);
        } catch (...) {
            // 如果解压失败，尝试直接反序列化（可能是未压缩的数据）
            actual_data = data;
        }
    }
    
    return TypedValue::deserialize(actual_data);
}

std::string BinarySerializer::serialize_batch(const std::vector<TypedValue>& values) {
    std::ostringstream oss;
    
    // 写入数量
    uint32_t count = values.size();
    oss.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    // 序列化每个值
    for (const auto& value : values) {
        std::string value_data = value.serialize();
        uint32_t size = value_data.size();
        oss.write(reinterpret_cast<const char*>(&size), sizeof(size));
        oss.write(value_data.data(), size);
    }
    
    std::string result = oss.str();
    
    if (compression_enabled_ && result.size() > 128) {
        result = compress_data(result);
    }
    
    return result;
}

std::vector<TypedValue> BinarySerializer::deserialize_batch(const std::string& data) {
    std::string actual_data = data;
    
    if (compression_enabled_) {
        try {
            actual_data = decompress_data(data);
        } catch (...) {
            actual_data = data;
        }
    }
    
    std::istringstream iss(actual_data);
    std::vector<TypedValue> result;
    
    // 读取数量
    uint32_t count;
    iss.read(reinterpret_cast<char*>(&count), sizeof(count));
    
    result.reserve(count);
    
    // 反序列化每个值
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t size;
        iss.read(reinterpret_cast<char*>(&size), sizeof(size));
        
        std::string value_data(size, '\0');
        iss.read(&value_data[0], size);
        
        result.push_back(TypedValue::deserialize(value_data));
    }
    
    return result;
}

size_t BinarySerializer::estimate_size(const TypedValue& value) const {
    size_t base_size = value.size() + sizeof(DataType);
    
    if (compression_enabled_ && base_size > 64) {
        // 估算压缩后的大小（通常为原大小的 30-70%）
        return base_size / 2;
    }
    
    return base_size;
}

std::string BinarySerializer::compress_data(const std::string& data) const {
    if (data.empty()) return data;
    
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    
    if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK) {
        throw std::runtime_error("Failed to initialize compression");
    }
    
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = data.size();
    
    int ret;
    char outbuffer[32768];
    std::string compressed;
    
    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);
        
        ret = deflate(&zs, Z_FINISH);
        
        if (compressed.size() < zs.total_out) {
            compressed.append(outbuffer, zs.total_out - compressed.size());
        }
    } while (ret == Z_OK);
    
    deflateEnd(&zs);
    
    if (ret != Z_STREAM_END) {
        throw std::runtime_error("Compression failed");
    }
    
    // 添加压缩标记
    std::string result;
    result.push_back('\x01');  // 压缩标记
    result.append(compressed);
    
    return result;
}

std::string BinarySerializer::decompress_data(const std::string& data) const {
    if (data.empty()) return data;
    
    // 检查压缩标记
    if (data[0] != '\x01') {
        return data;  // 未压缩的数据
    }
    
    std::string compressed_data = data.substr(1);
    
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    
    if (inflateInit(&zs) != Z_OK) {
        throw std::runtime_error("Failed to initialize decompression");
    }
    
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed_data.data()));
    zs.avail_in = compressed_data.size();
    
    int ret;
    char outbuffer[32768];
    std::string decompressed;
    
    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);
        
        ret = inflate(&zs, 0);
        
        if (decompressed.size() < zs.total_out) {
            decompressed.append(outbuffer, zs.total_out - decompressed.size());
        }
    } while (ret == Z_OK);
    
    inflateEnd(&zs);
    
    if (ret != Z_STREAM_END) {
        throw std::runtime_error("Decompression failed");
    }
    
    return decompressed;
}

} // namespace kvdb