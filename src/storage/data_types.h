#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdint>
#include <chrono>
#include <variant>
#include <memory>

namespace kvdb {

// 数据类型枚举
enum class DataType {
    // 数值类型
    INT,
    FLOAT,
    DOUBLE,
    
    // 字符串类型
    STRING,
    
    // 时间类型
    TIMESTAMP,
    DATE,
    
    // 集合类型
    LIST,
    SET,
    MAP,
    
    // 二进制类型
    BLOB,
    
    // 空值
    NULL_TYPE
};

// 时间戳类型
using Timestamp = std::chrono::system_clock::time_point;

// 日期类型 (年-月-日)
struct Date {
    int year;
    int month;
    int day;
    
    Date() : year(0), month(0), day(0) {}
    Date(int y, int m, int d) : year(y), month(m), day(d) {}
    
    std::string to_string() const;
    static Date from_string(const std::string& str);
    
    bool operator==(const Date& other) const {
        return year == other.year && month == other.month && day == other.day;
    }
    
    bool operator<(const Date& other) const {
        if (year != other.year) return year < other.year;
        if (month != other.month) return month < other.month;
        return day < other.day;
    }
};

// 二进制数据类型
using Blob = std::vector<uint8_t>;

// 前向声明
class TypedValue;

// 集合类型定义
using List = std::vector<TypedValue>;
using Set = std::set<TypedValue>;
using Map = std::map<std::string, TypedValue>;

// 类型化的值
class TypedValue {
public:
    using ValueVariant = std::variant<
        std::monostate,     // NULL_TYPE
        int64_t,            // INT
        float,              // FLOAT
        double,             // DOUBLE
        std::string,        // STRING
        Timestamp,          // TIMESTAMP
        Date,               // DATE
        std::shared_ptr<List>,   // LIST
        std::shared_ptr<Set>,    // SET
        std::shared_ptr<Map>,    // MAP
        Blob                // BLOB
    >;

private:
    DataType type_;
    ValueVariant value_;

public:
    // 构造函数
    TypedValue() : type_(DataType::NULL_TYPE), value_(std::monostate{}) {}
    
    explicit TypedValue(int64_t val) : type_(DataType::INT), value_(val) {}
    explicit TypedValue(int val) : type_(DataType::INT), value_(static_cast<int64_t>(val)) {}
    explicit TypedValue(float val) : type_(DataType::FLOAT), value_(val) {}
    explicit TypedValue(double val) : type_(DataType::DOUBLE), value_(val) {}
    explicit TypedValue(const std::string& val) : type_(DataType::STRING), value_(val) {}
    explicit TypedValue(const char* val) : type_(DataType::STRING), value_(std::string(val)) {}
    explicit TypedValue(const Timestamp& val) : type_(DataType::TIMESTAMP), value_(val) {}
    explicit TypedValue(const Date& val) : type_(DataType::DATE), value_(val) {}
    explicit TypedValue(const List& val) : type_(DataType::LIST), value_(std::make_shared<List>(val)) {}
    explicit TypedValue(const Set& val) : type_(DataType::SET), value_(std::make_shared<Set>(val)) {}
    explicit TypedValue(const Map& val) : type_(DataType::MAP), value_(std::make_shared<Map>(val)) {}
    explicit TypedValue(const Blob& val) : type_(DataType::BLOB), value_(val) {}
    
    // 获取类型
    DataType get_type() const { return type_; }
    
    // 类型检查
    bool is_null() const { return type_ == DataType::NULL_TYPE; }
    bool is_int() const { return type_ == DataType::INT; }
    bool is_float() const { return type_ == DataType::FLOAT; }
    bool is_double() const { return type_ == DataType::DOUBLE; }
    bool is_string() const { return type_ == DataType::STRING; }
    bool is_timestamp() const { return type_ == DataType::TIMESTAMP; }
    bool is_date() const { return type_ == DataType::DATE; }
    bool is_list() const { return type_ == DataType::LIST; }
    bool is_set() const { return type_ == DataType::SET; }
    bool is_map() const { return type_ == DataType::MAP; }
    bool is_blob() const { return type_ == DataType::BLOB; }
    
    // 值获取
    int64_t as_int() const;
    float as_float() const;
    double as_double() const;
    const std::string& as_string() const;
    const Timestamp& as_timestamp() const;
    const Date& as_date() const;
    const List& as_list() const;
    const Set& as_set() const;
    const Map& as_map() const;
    const Blob& as_blob() const;
    
    // 修改集合类型的值
    List& mutable_list();
    Set& mutable_set();
    Map& mutable_map();
    
    // 序列化和反序列化
    std::string serialize() const;
    static TypedValue deserialize(const std::string& data);
    
    // 比较操作
    bool operator==(const TypedValue& other) const;
    bool operator<(const TypedValue& other) const;
    bool operator!=(const TypedValue& other) const { return !(*this == other); }
    
    // 转换为字符串表示
    std::string to_string() const;
    
    // 类型转换
    TypedValue convert_to(DataType target_type) const;
    
    // 获取大小（字节数）
    size_t size() const;
};

// 工具函数
std::string data_type_to_string(DataType type);
DataType string_to_data_type(const std::string& type_str);

// 时间工具函数
Timestamp parse_timestamp(const std::string& str);
std::string format_timestamp(const Timestamp& ts);
Date parse_date(const std::string& str);
std::string format_date(const Date& date);

} // namespace kvdb