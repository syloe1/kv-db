#include "json_serializer.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cctype>

namespace kvdb {

std::string JsonSerializer::serialize(const TypedValue& value) {
    return typed_value_to_json_string(value, 0);
}

TypedValue JsonSerializer::deserialize(const std::string& data) {
    try {
        return parse_json_string(data);
    } catch (const std::exception& e) {
        throw std::runtime_error("JSON deserialization failed: " + std::string(e.what()));
    }
}

std::string JsonSerializer::serialize_batch(const std::vector<TypedValue>& values) {
    std::ostringstream oss;
    oss << "[";
    
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) oss << ",";
        if (pretty_print_) oss << "\n  ";
        oss << typed_value_to_json_string(values[i], pretty_print_ ? 2 : 0);
    }
    
    if (pretty_print_ && !values.empty()) oss << "\n";
    oss << "]";
    
    return oss.str();
}

std::vector<TypedValue> JsonSerializer::deserialize_batch(const std::string& data) {
    try {
        JsonParser parser(data, this);
        parser.skip_whitespace();
        
        if (parser.peek() != '[') {
            throw std::runtime_error("Expected JSON array for batch deserialization");
        }
        
        std::vector<TypedValue> array = parser.parse_array();
        return array;
    } catch (const std::exception& e) {
        throw std::runtime_error("JSON batch deserialization failed: " + std::string(e.what()));
    }
}

size_t JsonSerializer::estimate_size(const TypedValue& value) const {
    // JSON 通常比二进制格式大 2-4 倍
    size_t base_size = value.size();
    
    switch (value.get_type()) {
        case DataType::STRING:
            return base_size + 20;  // 引号和转义字符
        case DataType::LIST:
        case DataType::SET:
        case DataType::MAP:
            return base_size * 3;   // 结构化数据开销较大
        default:
            return base_size * 2;
    }
}

std::string JsonSerializer::typed_value_to_json_string(const TypedValue& value, int indent) const {
    std::ostringstream oss;
    std::string indent_str = pretty_print_ ? std::string(indent, ' ') : "";
    std::string next_indent_str = pretty_print_ ? std::string(indent + 2, ' ') : "";
    
    oss << "{";
    if (pretty_print_) oss << "\n" << next_indent_str;
    
    // 添加类型信息
    oss << "\"type\":\"" << data_type_to_string(value.get_type()) << "\"";
    oss << ",";
    if (pretty_print_) oss << "\n" << next_indent_str;
    oss << "\"value\":";
    
    switch (value.get_type()) {
        case DataType::NULL_TYPE:
            oss << "null";
            break;
            
        case DataType::INT:
            oss << value.as_int();
            break;
            
        case DataType::FLOAT:
            oss << std::fixed << std::setprecision(6) << value.as_float();
            break;
            
        case DataType::DOUBLE:
            oss << std::fixed << std::setprecision(15) << value.as_double();
            break;
            
        case DataType::STRING:
            oss << "\"" << escape_json_string(value.as_string()) << "\"";
            break;
            
        case DataType::TIMESTAMP: {
            auto ts = value.as_timestamp();
            auto time_t_val = std::chrono::system_clock::to_time_t(ts);
            std::ostringstream ts_oss;
            ts_oss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%SZ");
            oss << "\"" << ts_oss.str() << "\"";
            break;
        }
        
        case DataType::DATE: {
            const auto& date = value.as_date();
            oss << "\"" << date.to_string() << "\"";
            break;
        }
        
        case DataType::LIST: {
            const auto& list = value.as_list();
            oss << "[";
            for (size_t i = 0; i < list.size(); ++i) {
                if (i > 0) oss << ",";
                if (pretty_print_) oss << "\n" << std::string(indent + 4, ' ');
                oss << typed_value_to_json_string(list[i], indent + 4);
            }
            if (pretty_print_ && !list.empty()) oss << "\n" << next_indent_str;
            oss << "]";
            break;
        }
        
        case DataType::SET: {
            const auto& set = value.as_set();
            oss << "[";
            bool first = true;
            for (const auto& item : set) {
                if (!first) oss << ",";
                if (pretty_print_) oss << "\n" << std::string(indent + 4, ' ');
                oss << typed_value_to_json_string(item, indent + 4);
                first = false;
            }
            if (pretty_print_ && !set.empty()) oss << "\n" << next_indent_str;
            oss << "]";
            break;
        }
        
        case DataType::MAP: {
            const auto& map = value.as_map();
            oss << "{";
            bool first = true;
            for (const auto& [key, val] : map) {
                if (!first) oss << ",";
                if (pretty_print_) oss << "\n" << std::string(indent + 4, ' ');
                oss << "\"" << escape_json_string(key) << "\":";
                oss << typed_value_to_json_string(val, indent + 4);
                first = false;
            }
            if (pretty_print_ && !map.empty()) oss << "\n" << next_indent_str;
            oss << "}";
            break;
        }
        
        case DataType::BLOB: {
            const auto& blob = value.as_blob();
            std::string base64_data = encode_base64(blob);
            oss << "\"" << base64_data << "\"";
            break;
        }
    }
    
    if (pretty_print_) oss << "\n" << indent_str;
    oss << "}";
    
    return oss.str();
}

TypedValue JsonSerializer::parse_json_string(const std::string& json) const {
    JsonParser parser(json, this);
    return parser.parse_value();
}

std::string JsonSerializer::escape_json_string(const std::string& str) const {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    oss << c;
                }
                break;
        }
    }
    return oss.str();
}

std::string JsonSerializer::unescape_json_string(const std::string& str) const {
    std::ostringstream oss;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            switch (str[i + 1]) {
                case '"': oss << '"'; i++; break;
                case '\\': oss << '\\'; i++; break;
                case 'b': oss << '\b'; i++; break;
                case 'f': oss << '\f'; i++; break;
                case 'n': oss << '\n'; i++; break;
                case 'r': oss << '\r'; i++; break;
                case 't': oss << '\t'; i++; break;
                case 'u':
                    if (i + 5 < str.length()) {
                        // 简化的 Unicode 处理
                        i += 5;
                        oss << '?';  // 占位符
                    } else {
                        oss << str[i];
                    }
                    break;
                default:
                    oss << str[i];
                    break;
            }
        } else {
            oss << str[i];
        }
    }
    return oss.str();
}

std::string JsonSerializer::encode_base64(const std::vector<uint8_t>& data) const {
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t val = 0;
        int padding = 0;
        
        for (int j = 0; j < 3; ++j) {
            val <<= 8;
            if (i + j < data.size()) {
                val |= data[i + j];
            } else {
                padding++;
            }
        }
        
        for (int j = 0; j < 4; ++j) {
            if (j < 4 - padding) {
                result += chars[(val >> (18 - j * 6)) & 0x3F];
            } else {
                result += '=';
            }
        }
    }
    
    return result;
}

std::vector<uint8_t> JsonSerializer::decode_base64(const std::string& str) const {
    // 简化的 Base64 解码实现
    std::vector<uint8_t> result;
    
    if (str.empty()) return result;
    
    // Base64 字符表
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    // 创建反向查找表
    int decode_table[256];
    std::fill(decode_table, decode_table + 256, -1);
    for (size_t i = 0; i < chars.size(); ++i) {
        decode_table[static_cast<unsigned char>(chars[i])] = i;
    }
    
    // 解码
    for (size_t i = 0; i < str.length(); i += 4) {
        uint32_t val = 0;
        int padding = 0;
        
        for (int j = 0; j < 4; ++j) {
            val <<= 6;
            if (i + j < str.length()) {
                char c = str[i + j];
                if (c == '=') {
                    padding++;
                } else {
                    int decoded = decode_table[static_cast<unsigned char>(c)];
                    if (decoded >= 0) {
                        val |= decoded;
                    }
                }
            }
        }
        
        // 提取字节
        for (int j = 0; j < 3 - padding; ++j) {
            result.push_back(static_cast<uint8_t>((val >> (16 - j * 8)) & 0xFF));
        }
    }
    
    return result;
}

// JsonParser 实现
void JsonSerializer::JsonParser::skip_whitespace() {
    while (pos < json.length() && std::isspace(json[pos])) {
        pos++;
    }
}

char JsonSerializer::JsonParser::peek() {
    skip_whitespace();
    return pos < json.length() ? json[pos] : '\0';
}

char JsonSerializer::JsonParser::next() {
    skip_whitespace();
    return pos < json.length() ? json[pos++] : '\0';
}

std::string JsonSerializer::JsonParser::parse_string() {
    if (next() != '"') {
        throw std::runtime_error("Expected '\"' at start of string");
    }
    
    std::ostringstream oss;
    while (pos < json.length() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.length()) {
            pos++;
            switch (json[pos]) {
                case '"': oss << '"'; break;
                case '\\': oss << '\\'; break;
                case 'n': oss << '\n'; break;
                case 'r': oss << '\r'; break;
                case 't': oss << '\t'; break;
                default: oss << json[pos]; break;
            }
        } else {
            oss << json[pos];
        }
        pos++;
    }
    
    if (pos >= json.length()) {
        throw std::runtime_error("Unterminated string");
    }
    
    pos++; // skip closing quote
    return oss.str();
}

double JsonSerializer::JsonParser::parse_number() {
    size_t start = pos;
    
    if (json[pos] == '-') pos++;
    
    while (pos < json.length() && std::isdigit(json[pos])) {
        pos++;
    }
    
    if (pos < json.length() && json[pos] == '.') {
        pos++;
        while (pos < json.length() && std::isdigit(json[pos])) {
            pos++;
        }
    }
    
    if (pos < json.length() && (json[pos] == 'e' || json[pos] == 'E')) {
        pos++;
        if (pos < json.length() && (json[pos] == '+' || json[pos] == '-')) {
            pos++;
        }
        while (pos < json.length() && std::isdigit(json[pos])) {
            pos++;
        }
    }
    
    std::string num_str = json.substr(start, pos - start);
    return std::stod(num_str);
}

TypedValue JsonSerializer::JsonParser::parse_value() {
    char c = peek();
    
    if (c == '{') {
        // 解析对象（TypedValue）
        next(); // consume '{'
        
        std::string type_str;
        TypedValue result;
        
        while (peek() != '}') {
            skip_whitespace();
            if (peek() == ',') {
                next();
                continue;
            }
            
            std::string key = parse_string();
            skip_whitespace();
            if (next() != ':') {
                throw std::runtime_error("Expected ':' after key");
            }
            
            if (key == "type") {
                type_str = parse_string();
            } else if (key == "value") {
                // 根据类型解析值
                DataType type = string_to_data_type(type_str);
                
                switch (type) {
                    case DataType::NULL_TYPE:
                        // 跳过 null
                        if (json.substr(pos, 4) == "null") {
                            pos += 4;
                        }
                        result = TypedValue();
                        break;
                        
                    case DataType::INT: {
                        double val = parse_number();
                        result = TypedValue(static_cast<int64_t>(val));
                        break;
                    }
                    
                    case DataType::FLOAT: {
                        double val = parse_number();
                        result = TypedValue(static_cast<float>(val));
                        break;
                    }
                    
                    case DataType::DOUBLE: {
                        double val = parse_number();
                        result = TypedValue(val);
                        break;
                    }
                    
                    case DataType::STRING: {
                        std::string str = parse_string();
                        result = TypedValue(str);
                        break;
                    }
                    
                    case DataType::DATE: {
                        std::string date_str = parse_string();
                        result = TypedValue(Date::from_string(date_str));
                        break;
                    }
                    
                    case DataType::LIST: {
                        std::vector<TypedValue> array = parse_array();
                        List list(array.begin(), array.end());
                        result = TypedValue(list);
                        break;
                    }
                    
                    case DataType::MAP: {
                        std::map<std::string, TypedValue> obj = parse_object();
                        Map map(obj.begin(), obj.end());
                        result = TypedValue(map);
                        break;
                    }
                    
                    case DataType::BLOB: {
                        std::string blob_str = parse_string();
                        std::vector<uint8_t> blob = serializer->decode_base64(blob_str);
                        result = TypedValue(blob);
                        break;
                    }
                    
                    default:
                        throw std::runtime_error("Unsupported type in JSON parsing: " + type_str);
                }
            }
        }
        
        next(); // consume '}'
        return result;
    }
    
    throw std::runtime_error("Expected JSON object for TypedValue");
}

std::vector<TypedValue> JsonSerializer::JsonParser::parse_array() {
    if (next() != '[') {
        throw std::runtime_error("Expected '[' at start of array");
    }
    
    std::vector<TypedValue> result;
    
    while (peek() != ']') {
        if (!result.empty()) {
            if (next() != ',') {
                throw std::runtime_error("Expected ',' between array elements");
            }
        }
        result.push_back(parse_value());
    }
    
    next(); // consume ']'
    return result;
}

std::map<std::string, TypedValue> JsonSerializer::JsonParser::parse_object() {
    if (next() != '{') {
        throw std::runtime_error("Expected '{' at start of object");
    }
    
    std::map<std::string, TypedValue> result;
    
    while (peek() != '}') {
        if (!result.empty()) {
            if (next() != ',') {
                throw std::runtime_error("Expected ',' between object members");
            }
        }
        
        std::string key = parse_string();
        skip_whitespace();
        if (next() != ':') {
            throw std::runtime_error("Expected ':' after key");
        }
        
        result[key] = parse_value();
    }
    
    next(); // consume '}'
    return result;
}

} // namespace kvdb