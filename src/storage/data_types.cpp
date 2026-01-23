#include "data_types.h"
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace kvdb {

// Date 实现
std::string Date::to_string() const {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << year << "-"
        << std::setw(2) << month << "-"
        << std::setw(2) << day;
    return oss.str();
}

Date Date::from_string(const std::string& str) {
    Date date;
    char delimiter;
    std::istringstream iss(str);
    iss >> date.year >> delimiter >> date.month >> delimiter >> date.day;
    if (iss.fail()) {
        throw std::invalid_argument("Invalid date format: " + str);
    }
    return date;
}

// TypedValue 实现
int64_t TypedValue::as_int() const {
    if (type_ != DataType::INT) {
        throw std::runtime_error("Value is not an integer");
    }
    return std::get<int64_t>(value_);
}

float TypedValue::as_float() const {
    if (type_ != DataType::FLOAT) {
        throw std::runtime_error("Value is not a float");
    }
    return std::get<float>(value_);
}

double TypedValue::as_double() const {
    if (type_ != DataType::DOUBLE) {
        throw std::runtime_error("Value is not a double");
    }
    return std::get<double>(value_);
}

const std::string& TypedValue::as_string() const {
    if (type_ != DataType::STRING) {
        throw std::runtime_error("Value is not a string");
    }
    return std::get<std::string>(value_);
}

const Timestamp& TypedValue::as_timestamp() const {
    if (type_ != DataType::TIMESTAMP) {
        throw std::runtime_error("Value is not a timestamp");
    }
    return std::get<Timestamp>(value_);
}

const Date& TypedValue::as_date() const {
    if (type_ != DataType::DATE) {
        throw std::runtime_error("Value is not a date");
    }
    return std::get<Date>(value_);
}

const List& TypedValue::as_list() const {
    if (type_ != DataType::LIST) {
        throw std::runtime_error("Value is not a list");
    }
    return *std::get<std::shared_ptr<List>>(value_);
}

const Set& TypedValue::as_set() const {
    if (type_ != DataType::SET) {
        throw std::runtime_error("Value is not a set");
    }
    return *std::get<std::shared_ptr<Set>>(value_);
}

const Map& TypedValue::as_map() const {
    if (type_ != DataType::MAP) {
        throw std::runtime_error("Value is not a map");
    }
    return *std::get<std::shared_ptr<Map>>(value_);
}

const Blob& TypedValue::as_blob() const {
    if (type_ != DataType::BLOB) {
        throw std::runtime_error("Value is not a blob");
    }
    return std::get<Blob>(value_);
}

List& TypedValue::mutable_list() {
    if (type_ != DataType::LIST) {
        throw std::runtime_error("Value is not a list");
    }
    return *std::get<std::shared_ptr<List>>(value_);
}

Set& TypedValue::mutable_set() {
    if (type_ != DataType::SET) {
        throw std::runtime_error("Value is not a set");
    }
    return *std::get<std::shared_ptr<Set>>(value_);
}

Map& TypedValue::mutable_map() {
    if (type_ != DataType::MAP) {
        throw std::runtime_error("Value is not a map");
    }
    return *std::get<std::shared_ptr<Map>>(value_);
}

std::string TypedValue::serialize() const {
    std::ostringstream oss;
    
    // 写入类型标识
    oss.write(reinterpret_cast<const char*>(&type_), sizeof(type_));
    
    switch (type_) {
        case DataType::NULL_TYPE:
            // 空值不需要额外数据
            break;
            
        case DataType::INT: {
            int64_t val = std::get<int64_t>(value_);
            oss.write(reinterpret_cast<const char*>(&val), sizeof(val));
            break;
        }
        
        case DataType::FLOAT: {
            float val = std::get<float>(value_);
            oss.write(reinterpret_cast<const char*>(&val), sizeof(val));
            break;
        }
        
        case DataType::DOUBLE: {
            double val = std::get<double>(value_);
            oss.write(reinterpret_cast<const char*>(&val), sizeof(val));
            break;
        }
        
        case DataType::STRING: {
            const std::string& str = std::get<std::string>(value_);
            uint32_t len = str.length();
            oss.write(reinterpret_cast<const char*>(&len), sizeof(len));
            oss.write(str.data(), len);
            break;
        }
        
        case DataType::TIMESTAMP: {
            const auto& ts = std::get<Timestamp>(value_);
            auto duration = ts.time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            oss.write(reinterpret_cast<const char*>(&millis), sizeof(millis));
            break;
        }
        
        case DataType::DATE: {
            const Date& date = std::get<Date>(value_);
            oss.write(reinterpret_cast<const char*>(&date.year), sizeof(date.year));
            oss.write(reinterpret_cast<const char*>(&date.month), sizeof(date.month));
            oss.write(reinterpret_cast<const char*>(&date.day), sizeof(date.day));
            break;
        }
        
        case DataType::LIST: {
            const auto& list = *std::get<std::shared_ptr<List>>(value_);
            uint32_t size = list.size();
            oss.write(reinterpret_cast<const char*>(&size), sizeof(size));
            for (const auto& item : list) {
                std::string item_data = item.serialize();
                uint32_t item_len = item_data.length();
                oss.write(reinterpret_cast<const char*>(&item_len), sizeof(item_len));
                oss.write(item_data.data(), item_len);
            }
            break;
        }
        
        case DataType::SET: {
            const auto& set = *std::get<std::shared_ptr<Set>>(value_);
            uint32_t size = set.size();
            oss.write(reinterpret_cast<const char*>(&size), sizeof(size));
            for (const auto& item : set) {
                std::string item_data = item.serialize();
                uint32_t item_len = item_data.length();
                oss.write(reinterpret_cast<const char*>(&item_len), sizeof(item_len));
                oss.write(item_data.data(), item_len);
            }
            break;
        }
        
        case DataType::MAP: {
            const auto& map = *std::get<std::shared_ptr<Map>>(value_);
            uint32_t size = map.size();
            oss.write(reinterpret_cast<const char*>(&size), sizeof(size));
            for (const auto& [key, value] : map) {
                // 序列化键
                uint32_t key_len = key.length();
                oss.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
                oss.write(key.data(), key_len);
                
                // 序列化值
                std::string value_data = value.serialize();
                uint32_t value_len = value_data.length();
                oss.write(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
                oss.write(value_data.data(), value_len);
            }
            break;
        }
        
        case DataType::BLOB: {
            const Blob& blob = std::get<Blob>(value_);
            uint32_t size = blob.size();
            oss.write(reinterpret_cast<const char*>(&size), sizeof(size));
            oss.write(reinterpret_cast<const char*>(blob.data()), size);
            break;
        }
    }
    
    return oss.str();
}

TypedValue TypedValue::deserialize(const std::string& data) {
    std::istringstream iss(data);
    
    // 读取类型标识
    DataType type;
    iss.read(reinterpret_cast<char*>(&type), sizeof(type));
    
    TypedValue result;
    result.type_ = type;
    
    switch (type) {
        case DataType::NULL_TYPE:
            result.value_ = std::monostate{};
            break;
            
        case DataType::INT: {
            int64_t val;
            iss.read(reinterpret_cast<char*>(&val), sizeof(val));
            result.value_ = val;
            break;
        }
        
        case DataType::FLOAT: {
            float val;
            iss.read(reinterpret_cast<char*>(&val), sizeof(val));
            result.value_ = val;
            break;
        }
        
        case DataType::DOUBLE: {
            double val;
            iss.read(reinterpret_cast<char*>(&val), sizeof(val));
            result.value_ = val;
            break;
        }
        
        case DataType::STRING: {
            uint32_t len;
            iss.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::string str(len, '\0');
            iss.read(&str[0], len);
            result.value_ = str;
            break;
        }
        
        case DataType::TIMESTAMP: {
            int64_t millis;
            iss.read(reinterpret_cast<char*>(&millis), sizeof(millis));
            auto duration = std::chrono::milliseconds(millis);
            Timestamp ts(duration);
            result.value_ = ts;
            break;
        }
        
        case DataType::DATE: {
            Date date;
            iss.read(reinterpret_cast<char*>(&date.year), sizeof(date.year));
            iss.read(reinterpret_cast<char*>(&date.month), sizeof(date.month));
            iss.read(reinterpret_cast<char*>(&date.day), sizeof(date.day));
            result.value_ = date;
            break;
        }
        
        case DataType::LIST: {
            uint32_t size;
            iss.read(reinterpret_cast<char*>(&size), sizeof(size));
            auto list = std::make_shared<List>();
            for (uint32_t i = 0; i < size; ++i) {
                uint32_t item_len;
                iss.read(reinterpret_cast<char*>(&item_len), sizeof(item_len));
                std::string item_data(item_len, '\0');
                iss.read(&item_data[0], item_len);
                list->push_back(TypedValue::deserialize(item_data));
            }
            result.value_ = list;
            break;
        }
        
        case DataType::SET: {
            uint32_t size;
            iss.read(reinterpret_cast<char*>(&size), sizeof(size));
            auto set = std::make_shared<Set>();
            for (uint32_t i = 0; i < size; ++i) {
                uint32_t item_len;
                iss.read(reinterpret_cast<char*>(&item_len), sizeof(item_len));
                std::string item_data(item_len, '\0');
                iss.read(&item_data[0], item_len);
                set->insert(TypedValue::deserialize(item_data));
            }
            result.value_ = set;
            break;
        }
        
        case DataType::MAP: {
            uint32_t size;
            iss.read(reinterpret_cast<char*>(&size), sizeof(size));
            auto map = std::make_shared<Map>();
            for (uint32_t i = 0; i < size; ++i) {
                // 读取键
                uint32_t key_len;
                iss.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
                std::string key(key_len, '\0');
                iss.read(&key[0], key_len);
                
                // 读取值
                uint32_t value_len;
                iss.read(reinterpret_cast<char*>(&value_len), sizeof(value_len));
                std::string value_data(value_len, '\0');
                iss.read(&value_data[0], value_len);
                
                (*map)[key] = TypedValue::deserialize(value_data);
            }
            result.value_ = map;
            break;
        }
        
        case DataType::BLOB: {
            uint32_t size;
            iss.read(reinterpret_cast<char*>(&size), sizeof(size));
            Blob blob(size);
            iss.read(reinterpret_cast<char*>(blob.data()), size);
            result.value_ = blob;
            break;
        }
    }
    
    return result;
}

bool TypedValue::operator==(const TypedValue& other) const {
    if (type_ != other.type_) {
        return false;
    }
    
    return value_ == other.value_;
}

bool TypedValue::operator<(const TypedValue& other) const {
    if (type_ != other.type_) {
        return type_ < other.type_;
    }
    
    return value_ < other.value_;
}

std::string TypedValue::to_string() const {
    switch (type_) {
        case DataType::NULL_TYPE:
            return "NULL";
            
        case DataType::INT:
            return std::to_string(std::get<int64_t>(value_));
            
        case DataType::FLOAT:
            return std::to_string(std::get<float>(value_));
            
        case DataType::DOUBLE:
            return std::to_string(std::get<double>(value_));
            
        case DataType::STRING:
            return std::get<std::string>(value_);
            
        case DataType::TIMESTAMP:
            return format_timestamp(std::get<Timestamp>(value_));
            
        case DataType::DATE:
            return std::get<Date>(value_).to_string();
            
        case DataType::LIST: {
            const auto& list = *std::get<std::shared_ptr<List>>(value_);
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < list.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << list[i].to_string();
            }
            oss << "]";
            return oss.str();
        }
        
        case DataType::SET: {
            const auto& set = *std::get<std::shared_ptr<Set>>(value_);
            std::ostringstream oss;
            oss << "{";
            bool first = true;
            for (const auto& item : set) {
                if (!first) oss << ", ";
                oss << item.to_string();
                first = false;
            }
            oss << "}";
            return oss.str();
        }
        
        case DataType::MAP: {
            const auto& map = *std::get<std::shared_ptr<Map>>(value_);
            std::ostringstream oss;
            oss << "{";
            bool first = true;
            for (const auto& [key, value] : map) {
                if (!first) oss << ", ";
                oss << key << ": " << value.to_string();
                first = false;
            }
            oss << "}";
            return oss.str();
        }
        
        case DataType::BLOB: {
            const Blob& blob = std::get<Blob>(value_);
            std::ostringstream oss;
            oss << "BLOB(" << blob.size() << " bytes)";
            return oss.str();
        }
    }
    
    return "UNKNOWN";
}

TypedValue TypedValue::convert_to(DataType target_type) const {
    if (type_ == target_type) {
        return *this;
    }
    
    // 实现基本的类型转换
    switch (target_type) {
        case DataType::STRING:
            return TypedValue(to_string());
            
        case DataType::INT:
            if (type_ == DataType::FLOAT) {
                return TypedValue(static_cast<int64_t>(std::get<float>(value_)));
            } else if (type_ == DataType::DOUBLE) {
                return TypedValue(static_cast<int64_t>(std::get<double>(value_)));
            } else if (type_ == DataType::STRING) {
                try {
                    return TypedValue(static_cast<int64_t>(std::stoll(std::get<std::string>(value_))));
                } catch (...) {
                    throw std::runtime_error("Cannot convert string to int");
                }
            }
            break;
            
        case DataType::FLOAT:
            if (type_ == DataType::INT) {
                return TypedValue(static_cast<float>(std::get<int64_t>(value_)));
            } else if (type_ == DataType::DOUBLE) {
                return TypedValue(static_cast<float>(std::get<double>(value_)));
            }
            break;
            
        case DataType::DOUBLE:
            if (type_ == DataType::INT) {
                return TypedValue(static_cast<double>(std::get<int64_t>(value_)));
            } else if (type_ == DataType::FLOAT) {
                return TypedValue(static_cast<double>(std::get<float>(value_)));
            }
            break;
            
        default:
            break;
    }
    
    throw std::runtime_error("Unsupported type conversion");
}

size_t TypedValue::size() const {
    switch (type_) {
        case DataType::NULL_TYPE:
            return 0;
        case DataType::INT:
            return sizeof(int64_t);
        case DataType::FLOAT:
            return sizeof(float);
        case DataType::DOUBLE:
            return sizeof(double);
        case DataType::STRING:
            return std::get<std::string>(value_).size();
        case DataType::TIMESTAMP:
            return sizeof(Timestamp);
        case DataType::DATE:
            return sizeof(Date);
        case DataType::LIST: {
            size_t total = 0;
            for (const auto& item : *std::get<std::shared_ptr<List>>(value_)) {
                total += item.size();
            }
            return total;
        }
        case DataType::SET: {
            size_t total = 0;
            for (const auto& item : *std::get<std::shared_ptr<Set>>(value_)) {
                total += item.size();
            }
            return total;
        }
        case DataType::MAP: {
            size_t total = 0;
            for (const auto& [key, value] : *std::get<std::shared_ptr<Map>>(value_)) {
                total += key.size() + value.size();
            }
            return total;
        }
        case DataType::BLOB:
            return std::get<Blob>(value_).size();
    }
    return 0;
}

// 工具函数实现
std::string data_type_to_string(DataType type) {
    switch (type) {
        case DataType::NULL_TYPE: return "NULL";
        case DataType::INT: return "INT";
        case DataType::FLOAT: return "FLOAT";
        case DataType::DOUBLE: return "DOUBLE";
        case DataType::STRING: return "STRING";
        case DataType::TIMESTAMP: return "TIMESTAMP";
        case DataType::DATE: return "DATE";
        case DataType::LIST: return "LIST";
        case DataType::SET: return "SET";
        case DataType::MAP: return "MAP";
        case DataType::BLOB: return "BLOB";
    }
    return "UNKNOWN";
}

DataType string_to_data_type(const std::string& type_str) {
    std::string upper_str = type_str;
    std::transform(upper_str.begin(), upper_str.end(), upper_str.begin(), ::toupper);
    
    if (upper_str == "NULL") return DataType::NULL_TYPE;
    if (upper_str == "INT") return DataType::INT;
    if (upper_str == "FLOAT") return DataType::FLOAT;
    if (upper_str == "DOUBLE") return DataType::DOUBLE;
    if (upper_str == "STRING") return DataType::STRING;
    if (upper_str == "TIMESTAMP") return DataType::TIMESTAMP;
    if (upper_str == "DATE") return DataType::DATE;
    if (upper_str == "LIST") return DataType::LIST;
    if (upper_str == "SET") return DataType::SET;
    if (upper_str == "MAP") return DataType::MAP;
    if (upper_str == "BLOB") return DataType::BLOB;
    
    throw std::invalid_argument("Unknown data type: " + type_str);
}

// 时间工具函数实现
Timestamp parse_timestamp(const std::string& str) {
    // 简单的时间戳解析，支持格式: "YYYY-MM-DD HH:MM:SS"
    std::tm tm = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        throw std::invalid_argument("Invalid timestamp format: " + str);
    }
    
    auto time_t_val = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(time_t_val);
}

std::string format_timestamp(const Timestamp& ts) {
    auto time_t_val = std::chrono::system_clock::to_time_t(ts);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t_val), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

Date parse_date(const std::string& str) {
    return Date::from_string(str);
}

std::string format_date(const Date& date) {
    return date.to_string();
}

} // namespace kvdb