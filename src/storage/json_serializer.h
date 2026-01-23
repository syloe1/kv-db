#pragma once
#include "serialization_interface.h"
// 使用简化的 JSON 实现，不依赖外部库

namespace kvdb {

// JSON 序列化器
class JsonSerializer : public SerializationInterface {
public:
    JsonSerializer(bool pretty_print = false) : pretty_print_(pretty_print) {}
    
    std::string serialize(const TypedValue& value) override;
    TypedValue deserialize(const std::string& data) override;
    
    std::string serialize_batch(const std::vector<TypedValue>& values) override;
    std::vector<TypedValue> deserialize_batch(const std::string& data) override;
    
    SerializationFormat get_format() const override { return SerializationFormat::JSON; }
    std::string get_format_name() const override { return "JSON"; }
    
    size_t estimate_size(const TypedValue& value) const override;
    
    void set_pretty_print(bool enable) { pretty_print_ = enable; }
    
private:
    bool pretty_print_;
    
    // 简化的 JSON 处理
    std::string typed_value_to_json_string(const TypedValue& value, int indent = 0) const;
    TypedValue parse_json_string(const std::string& json) const;
    
    // JSON 解析辅助函数
    std::string escape_json_string(const std::string& str) const;
    std::string unescape_json_string(const std::string& str) const;
    std::string encode_base64(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> decode_base64(const std::string& str) const;
    
    // 简单的 JSON 解析器
    struct JsonParser {
        const std::string& json;
        size_t pos;
        const JsonSerializer* serializer;  // 添加对序列化器的引用
        
        JsonParser(const std::string& j, const JsonSerializer* s) : json(j), pos(0), serializer(s) {}
        
        void skip_whitespace();
        char peek();
        char next();
        std::string parse_string();
        double parse_number();
        TypedValue parse_value();
        std::vector<TypedValue> parse_array();
        std::map<std::string, TypedValue> parse_object();
    };
};

} // namespace kvdb