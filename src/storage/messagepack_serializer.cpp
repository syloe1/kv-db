#include "messagepack_serializer.h"
#include <zlib.h>

namespace kvdb {

std::string MessagePackSerializer::serialize(const TypedValue& value) {
    msgpack::zone zone;
    auto obj_handle = typed_value_to_msgpack(value, zone);
    
    std::stringstream ss;
    msgpack::pack(ss, obj_handle.get());
    
    std::string result = ss.str();
    
    if (compression_enabled_ && result.size() > 64) {
        result = compress_data(result);
    }
    
    return result;
}

TypedValue MessagePackSerializer::deserialize(const std::string& data) {
    std::string actual_data = data;
    
    if (compression_enabled_) {
        try {
            actual_data = decompress_data(data);
        } catch (...) {
            actual_data = data;
        }
    }
    
    try {
        msgpack::object_handle oh = msgpack::unpack(actual_data.data(), actual_data.size());
        return msgpack_to_typed_value(oh.get());
    } catch (const msgpack::exception& e) {
        throw std::runtime_error("MessagePack deserialization failed: " + std::string(e.what()));
    }
}

std::string MessagePackSerializer::serialize_batch(const std::vector<TypedValue>& values) {
    msgpack::zone zone;
    std::vector<msgpack::object> objects;
    objects.reserve(values.size());
    
    for (const auto& value : values) {
        auto obj_handle = typed_value_to_msgpack(value, zone);
        objects.push_back(obj_handle.get());
    }
    
    std::stringstream ss;
    msgpack::pack(ss, objects);
    
    std::string result = ss.str();
    
    if (compression_enabled_ && result.size() > 128) {
        result = compress_data(result);
    }
    
    return result;
}

std::vector<TypedValue> MessagePackSerializer::deserialize_batch(const std::string& data) {
    std::string actual_data = data;
    
    if (compression_enabled_) {
        try {
            actual_data = decompress_data(data);
        } catch (...) {
            actual_data = data;
        }
    }
    
    try {
        msgpack::object_handle oh = msgpack::unpack(actual_data.data(), actual_data.size());
        const msgpack::object& obj = oh.get();
        
        if (obj.type != msgpack::type::ARRAY) {
            throw std::runtime_error("Expected MessagePack array for batch deserialization");
        }
        
        std::vector<TypedValue> result;
        result.reserve(obj.via.array.size);
        
        for (uint32_t i = 0; i < obj.via.array.size; ++i) {
            result.push_back(msgpack_to_typed_value(obj.via.array.ptr[i]));
        }
        
        return result;
    } catch (const msgpack::exception& e) {
        throw std::runtime_error("MessagePack batch deserialization failed: " + std::string(e.what()));
    }
}

size_t MessagePackSerializer::estimate_size(const TypedValue& value) const {
    // MessagePack 通常比二进制格式稍大，但比 JSON 小
    size_t base_size = value.size();
    
    if (compression_enabled_ && base_size > 64) {
        return base_size / 2;  // 压缩后估算
    }
    
    return base_size + base_size / 4;  // 约 25% 的开销
}

msgpack::object_handle MessagePackSerializer::typed_value_to_msgpack(const TypedValue& value, msgpack::zone& zone) const {
    std::map<std::string, msgpack::object> obj_map;
    
    // 添加类型信息
    std::string type_str = data_type_to_string(value.get_type());
    obj_map["type"] = msgpack::object(type_str, zone);
    
    switch (value.get_type()) {
        case DataType::NULL_TYPE:
            obj_map["value"] = msgpack::object();
            break;
            
        case DataType::INT:
            obj_map["value"] = msgpack::object(value.as_int());
            break;
            
        case DataType::FLOAT:
            obj_map["value"] = msgpack::object(value.as_float());
            break;
            
        case DataType::DOUBLE:
            obj_map["value"] = msgpack::object(value.as_double());
            break;
            
        case DataType::STRING: {
            const std::string& str = value.as_string();
            obj_map["value"] = msgpack::object(str, zone);
            break;
        }
        
        case DataType::TIMESTAMP: {
            auto ts = value.as_timestamp();
            auto duration = ts.time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            obj_map["value"] = msgpack::object(millis);
            break;
        }
        
        case DataType::DATE: {
            const auto& date = value.as_date();
            std::string date_str = date.to_string();
            obj_map["value"] = msgpack::object(date_str, zone);
            break;
        }
        
        case DataType::LIST: {
            const auto& list = value.as_list();
            std::vector<msgpack::object> array;
            array.reserve(list.size());
            
            for (const auto& item : list) {
                auto item_handle = typed_value_to_msgpack(item, zone);
                array.push_back(item_handle.get());
            }
            
            obj_map["value"] = msgpack::object(array, zone);
            break;
        }
        
        case DataType::SET: {
            const auto& set = value.as_set();
            std::vector<msgpack::object> array;
            array.reserve(set.size());
            
            for (const auto& item : set) {
                auto item_handle = typed_value_to_msgpack(item, zone);
                array.push_back(item_handle.get());
            }
            
            obj_map["value"] = msgpack::object(array, zone);
            break;
        }
        
        case DataType::MAP: {
            const auto& map = value.as_map();
            std::map<std::string, msgpack::object> nested_map;
            
            for (const auto& [key, val] : map) {
                auto val_handle = typed_value_to_msgpack(val, zone);
                nested_map[key] = val_handle.get();
            }
            
            obj_map["value"] = msgpack::object(nested_map, zone);
            break;
        }
        
        case DataType::BLOB: {
            const auto& blob = value.as_blob();
            std::string blob_str(reinterpret_cast<const char*>(blob.data()), blob.size());
            obj_map["value"] = msgpack::object(blob_str, zone);
            break;
        }
    }
    
    return msgpack::object_handle(msgpack::object(obj_map, zone), msgpack::unique_ptr<msgpack::zone>(new msgpack::zone(std::move(zone))));
}

TypedValue MessagePackSerializer::msgpack_to_typed_value(const msgpack::object& obj) const {
    if (obj.type != msgpack::type::MAP) {
        throw std::runtime_error("Expected MessagePack map for TypedValue");
    }
    
    std::string type_str;
    msgpack::object value_obj;
    bool found_type = false, found_value = false;
    
    for (uint32_t i = 0; i < obj.via.map.size; ++i) {
        const auto& kv = obj.via.map.ptr[i];
        if (kv.key.type == msgpack::type::STR) {
            std::string key(kv.key.via.str.ptr, kv.key.via.str.size);
            if (key == "type") {
                if (kv.val.type == msgpack::type::STR) {
                    type_str = std::string(kv.val.via.str.ptr, kv.val.via.str.size);
                    found_type = true;
                }
            } else if (key == "value") {
                value_obj = kv.val;
                found_value = true;
            }
        }
    }
    
    if (!found_type || !found_value) {
        throw std::runtime_error("Invalid MessagePack format: missing type or value field");
    }
    
    DataType type = string_to_data_type(type_str);
    
    switch (type) {
        case DataType::NULL_TYPE:
            return TypedValue();
            
        case DataType::INT:
            if (value_obj.type == msgpack::type::POSITIVE_INTEGER) {
                return TypedValue(static_cast<int64_t>(value_obj.via.u64));
            } else if (value_obj.type == msgpack::type::NEGATIVE_INTEGER) {
                return TypedValue(value_obj.via.i64);
            }
            break;
            
        case DataType::FLOAT:
            if (value_obj.type == msgpack::type::FLOAT32) {
                return TypedValue(value_obj.via.f64);
            }
            break;
            
        case DataType::DOUBLE:
            if (value_obj.type == msgpack::type::FLOAT64) {
                return TypedValue(value_obj.via.f64);
            }
            break;
            
        case DataType::STRING:
            if (value_obj.type == msgpack::type::STR) {
                return TypedValue(std::string(value_obj.via.str.ptr, value_obj.via.str.size));
            }
            break;
            
        case DataType::TIMESTAMP:
            if (value_obj.type == msgpack::type::POSITIVE_INTEGER) {
                auto millis = static_cast<int64_t>(value_obj.via.u64);
                auto duration = std::chrono::milliseconds(millis);
                return TypedValue(Timestamp(duration));
            }
            break;
            
        case DataType::DATE:
            if (value_obj.type == msgpack::type::STR) {
                std::string date_str(value_obj.via.str.ptr, value_obj.via.str.size);
                return TypedValue(Date::from_string(date_str));
            }
            break;
            
        case DataType::LIST:
            if (value_obj.type == msgpack::type::ARRAY) {
                List list;
                for (uint32_t i = 0; i < value_obj.via.array.size; ++i) {
                    list.push_back(msgpack_to_typed_value(value_obj.via.array.ptr[i]));
                }
                return TypedValue(list);
            }
            break;
            
        case DataType::SET:
            if (value_obj.type == msgpack::type::ARRAY) {
                Set set;
                for (uint32_t i = 0; i < value_obj.via.array.size; ++i) {
                    set.insert(msgpack_to_typed_value(value_obj.via.array.ptr[i]));
                }
                return TypedValue(set);
            }
            break;
            
        case DataType::MAP:
            if (value_obj.type == msgpack::type::MAP) {
                Map map;
                for (uint32_t i = 0; i < value_obj.via.map.size; ++i) {
                    const auto& kv = value_obj.via.map.ptr[i];
                    if (kv.key.type == msgpack::type::STR) {
                        std::string key(kv.key.via.str.ptr, kv.key.via.str.size);
                        map[key] = msgpack_to_typed_value(kv.val);
                    }
                }
                return TypedValue(map);
            }
            break;
            
        case DataType::BLOB:
            if (value_obj.type == msgpack::type::STR) {
                std::string blob_str(value_obj.via.str.ptr, value_obj.via.str.size);
                Blob blob(blob_str.begin(), blob_str.end());
                return TypedValue(blob);
            }
            break;
    }
    
    throw std::runtime_error("Invalid MessagePack data for type: " + type_str);
}

std::string MessagePackSerializer::compress_data(const std::string& data) const {
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
    result.push_back('\x02');  // MessagePack 压缩标记
    result.append(compressed);
    
    return result;
}

std::string MessagePackSerializer::decompress_data(const std::string& data) const {
    if (data.empty()) return data;
    
    // 检查压缩标记
    if (data[0] != '\x02') {
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