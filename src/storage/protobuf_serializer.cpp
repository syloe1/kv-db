#include "protobuf_serializer.h"
#include <sstream>
#include <zlib.h>
#include <cstring>

namespace kvdb {

// 简化的 Protocol Buffers 实现
// 在实际项目中，应该使用真正的 protobuf 库和 .proto 文件

std::string ProtobufSerializer::serialize(const TypedValue& value) {
    std::ostringstream oss;
    
    // 简化的 protobuf 格式：tag + type + data
    uint8_t type_tag = static_cast<uint8_t>(value.get_type());
    oss.write(reinterpret_cast<const char*>(&type_tag), sizeof(type_tag));
    
    switch (value.get_type()) {
        case DataType::NULL_TYPE:
            // 空值不需要额外数据
            break;
            
        case DataType::INT: {
            int64_t val = value.as_int();
            // 使用 varint 编码
            write_varint(oss, static_cast<uint64_t>(val));
            break;
        }
        
        case DataType::FLOAT: {
            float val = value.as_float();
            oss.write(reinterpret_cast<const char*>(&val), sizeof(val));
            break;
        }
        
        case DataType::DOUBLE: {
            double val = value.as_double();
            oss.write(reinterpret_cast<const char*>(&val), sizeof(val));
            break;
        }
        
        case DataType::STRING: {
            const std::string& str = value.as_string();
            write_varint(oss, str.length());
            oss.write(str.data(), str.length());
            break;
        }
        
        case DataType::TIMESTAMP: {
            auto ts = value.as_timestamp();
            auto duration = ts.time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            write_varint(oss, static_cast<uint64_t>(millis));
            break;
        }
        
        case DataType::DATE: {
            const auto& date = value.as_date();
            write_varint(oss, static_cast<uint64_t>(date.year));
            write_varint(oss, static_cast<uint64_t>(date.month));
            write_varint(oss, static_cast<uint64_t>(date.day));
            break;
        }
        
        case DataType::LIST: {
            const auto& list = value.as_list();
            write_varint(oss, list.size());
            for (const auto& item : list) {
                std::string item_data = serialize(item);
                write_varint(oss, item_data.length());
                oss.write(item_data.data(), item_data.length());
            }
            break;
        }
        
        case DataType::SET: {
            const auto& set = value.as_set();
            write_varint(oss, set.size());
            for (const auto& item : set) {
                std::string item_data = serialize(item);
                write_varint(oss, item_data.length());
                oss.write(item_data.data(), item_data.length());
            }
            break;
        }
        
        case DataType::MAP: {
            const auto& map = value.as_map();
            write_varint(oss, map.size());
            for (const auto& [key, val] : map) {
                // 序列化键
                write_varint(oss, key.length());
                oss.write(key.data(), key.length());
                
                // 序列化值
                std::string val_data = serialize(val);
                write_varint(oss, val_data.length());
                oss.write(val_data.data(), val_data.length());
            }
            break;
        }
        
        case DataType::BLOB: {
            const auto& blob = value.as_blob();
            write_varint(oss, blob.size());
            oss.write(reinterpret_cast<const char*>(blob.data()), blob.size());
            break;
        }
    }
    
    std::string result = oss.str();
    
    if (compression_enabled_ && result.size() > 64) {
        result = compress_data(result);
    }
    
    return result;
}

TypedValue ProtobufSerializer::deserialize(const std::string& data) {
    std::string actual_data = data;
    
    if (compression_enabled_) {
        try {
            actual_data = decompress_data(data);
        } catch (...) {
            actual_data = data;
        }
    }
    
    std::istringstream iss(actual_data);
    
    // 读取类型标签
    uint8_t type_tag;
    iss.read(reinterpret_cast<char*>(&type_tag), sizeof(type_tag));
    DataType type = static_cast<DataType>(type_tag);
    
    switch (type) {
        case DataType::NULL_TYPE:
            return TypedValue();
            
        case DataType::INT: {
            uint64_t val = read_varint(iss);
            return TypedValue(static_cast<int64_t>(val));
        }
        
        case DataType::FLOAT: {
            float val;
            iss.read(reinterpret_cast<char*>(&val), sizeof(val));
            return TypedValue(val);
        }
        
        case DataType::DOUBLE: {
            double val;
            iss.read(reinterpret_cast<char*>(&val), sizeof(val));
            return TypedValue(val);
        }
        
        case DataType::STRING: {
            uint64_t len = read_varint(iss);
            std::string str(len, '\0');
            iss.read(&str[0], len);
            return TypedValue(str);
        }
        
        case DataType::TIMESTAMP: {
            uint64_t millis = read_varint(iss);
            auto duration = std::chrono::milliseconds(millis);
            return TypedValue(Timestamp(duration));
        }
        
        case DataType::DATE: {
            uint64_t year = read_varint(iss);
            uint64_t month = read_varint(iss);
            uint64_t day = read_varint(iss);
            return TypedValue(Date(static_cast<int>(year), static_cast<int>(month), static_cast<int>(day)));
        }
        
        case DataType::LIST: {
            uint64_t size = read_varint(iss);
            List list;
            list.reserve(size);
            
            for (uint64_t i = 0; i < size; ++i) {
                uint64_t item_len = read_varint(iss);
                std::string item_data(item_len, '\0');
                iss.read(&item_data[0], item_len);
                list.push_back(deserialize(item_data));
            }
            
            return TypedValue(list);
        }
        
        case DataType::SET: {
            uint64_t size = read_varint(iss);
            Set set;
            
            for (uint64_t i = 0; i < size; ++i) {
                uint64_t item_len = read_varint(iss);
                std::string item_data(item_len, '\0');
                iss.read(&item_data[0], item_len);
                set.insert(deserialize(item_data));
            }
            
            return TypedValue(set);
        }
        
        case DataType::MAP: {
            uint64_t size = read_varint(iss);
            Map map;
            
            for (uint64_t i = 0; i < size; ++i) {
                // 读取键
                uint64_t key_len = read_varint(iss);
                std::string key(key_len, '\0');
                iss.read(&key[0], key_len);
                
                // 读取值
                uint64_t val_len = read_varint(iss);
                std::string val_data(val_len, '\0');
                iss.read(&val_data[0], val_len);
                
                map[key] = deserialize(val_data);
            }
            
            return TypedValue(map);
        }
        
        case DataType::BLOB: {
            uint64_t size = read_varint(iss);
            Blob blob(size);
            iss.read(reinterpret_cast<char*>(blob.data()), size);
            return TypedValue(blob);
        }
    }
    
    throw std::runtime_error("Unknown data type in protobuf data");
}

std::string ProtobufSerializer::serialize_batch(const std::vector<TypedValue>& values) {
    std::ostringstream oss;
    
    // 写入数量
    write_varint(oss, values.size());
    
    // 序列化每个值
    for (const auto& value : values) {
        std::string value_data = serialize(value);
        write_varint(oss, value_data.size());
        oss.write(value_data.data(), value_data.size());
    }
    
    std::string result = oss.str();
    
    if (compression_enabled_ && result.size() > 128) {
        result = compress_data(result);
    }
    
    return result;
}

std::vector<TypedValue> ProtobufSerializer::deserialize_batch(const std::string& data) {
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
    uint64_t count = read_varint(iss);
    result.reserve(count);
    
    // 反序列化每个值
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t size = read_varint(iss);
        std::string value_data(size, '\0');
        iss.read(&value_data[0], size);
        result.push_back(deserialize(value_data));
    }
    
    return result;
}

size_t ProtobufSerializer::estimate_size(const TypedValue& value) const {
    // Protobuf 通常比二进制格式稍小，因为使用了 varint 编码
    size_t base_size = value.size();
    
    if (compression_enabled_ && base_size > 64) {
        return base_size / 2;  // 压缩后估算
    }
    
    return base_size - base_size / 8;  // 约节省 12.5%
}

void ProtobufSerializer::write_varint(std::ostream& os, uint64_t value) const {
    while (value >= 0x80) {
        uint8_t byte = static_cast<uint8_t>(value | 0x80);
        os.write(reinterpret_cast<const char*>(&byte), 1);
        value >>= 7;
    }
    uint8_t byte = static_cast<uint8_t>(value);
    os.write(reinterpret_cast<const char*>(&byte), 1);
}

uint64_t ProtobufSerializer::read_varint(std::istream& is) const {
    uint64_t result = 0;
    int shift = 0;
    
    while (true) {
        uint8_t byte;
        is.read(reinterpret_cast<char*>(&byte), 1);
        
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        
        if ((byte & 0x80) == 0) {
            break;
        }
        
        shift += 7;
        if (shift >= 64) {
            throw std::runtime_error("Varint too long");
        }
    }
    
    return result;
}

std::string ProtobufSerializer::compress_data(const std::string& data) const {
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
    result.push_back('\x03');  // Protobuf 压缩标记
    result.append(compressed);
    
    return result;
}

std::string ProtobufSerializer::decompress_data(const std::string& data) const {
    if (data.empty()) return data;
    
    // 检查压缩标记
    if (data[0] != '\x03') {
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