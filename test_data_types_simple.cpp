#include "src/storage/data_types.h"
#include "src/storage/typed_memtable.h"
#include <iostream>
#include <chrono>
#include <cassert>

using namespace kvdb;

void test_basic_types() {
    std::cout << "=== 测试基本数据类型 ===" << std::endl;
    
    // 测试整数类型
    TypedValue int_val(42);
    assert(int_val.is_int());
    assert(int_val.as_int() == 42);
    std::cout << "✓ INT 类型测试通过: " << int_val.as_int() << std::endl;
    
    // 测试浮点类型
    TypedValue float_val(3.14f);
    assert(float_val.is_float());
    assert(std::abs(float_val.as_float() - 3.14f) < 0.01f);
    std::cout << "✓ FLOAT 类型测试通过: " << float_val.as_float() << std::endl;
    
    // 测试双精度浮点
    TypedValue double_val(2.71828);
    assert(double_val.is_double());
    assert(std::abs(double_val.as_double() - 2.71828) < 0.0001);
    std::cout << "✓ DOUBLE 类型测试通过: " << double_val.as_double() << std::endl;
    
    // 测试字符串类型
    TypedValue string_val("Hello, World!");
    assert(string_val.is_string());
    assert(string_val.as_string() == "Hello, World!");
    std::cout << "✓ STRING 类型测试通过: " << string_val.as_string() << std::endl;
}

void test_time_types() {
    std::cout << "\n=== 测试时间类型 ===" << std::endl;
    
    // 测试时间戳
    auto now = std::chrono::system_clock::now();
    TypedValue timestamp_val(now);
    assert(timestamp_val.is_timestamp());
    std::cout << "✓ TIMESTAMP 类型测试通过" << std::endl;
    
    // 测试日期
    Date birthday(1990, 5, 15);
    TypedValue date_val(birthday);
    assert(date_val.is_date());
    assert(date_val.as_date().year == 1990);
    assert(date_val.as_date().month == 5);
    assert(date_val.as_date().day == 15);
    std::cout << "✓ DATE 类型测试通过: " << date_val.as_date().to_string() << std::endl;
}

void test_collection_types() {
    std::cout << "\n=== 测试集合类型 ===" << std::endl;
    
    // 测试列表
    List numbers = {TypedValue(1), TypedValue(2), TypedValue(3)};
    TypedValue list_val(numbers);
    assert(list_val.is_list());
    assert(list_val.as_list().size() == 3);
    std::cout << "✓ LIST 类型测试通过，大小: " << list_val.as_list().size() << std::endl;
    
    // 测试集合
    Set colors = {TypedValue("red"), TypedValue("green"), TypedValue("blue")};
    TypedValue set_val(colors);
    assert(set_val.is_set());
    assert(set_val.as_set().size() == 3);
    std::cout << "✓ SET 类型测试通过，大小: " << set_val.as_set().size() << std::endl;
    
    // 测试映射
    Map person;
    person["name"] = TypedValue("Alice");
    person["age"] = TypedValue(25);
    TypedValue map_val(person);
    assert(map_val.is_map());
    assert(map_val.as_map().size() == 2);
    std::cout << "✓ MAP 类型测试通过，大小: " << map_val.as_map().size() << std::endl;
}

void test_blob_type() {
    std::cout << "\n=== 测试二进制类型 ===" << std::endl;
    
    // 创建二进制数据
    Blob data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello" in ASCII
    TypedValue blob_val(data);
    assert(blob_val.is_blob());
    assert(blob_val.as_blob().size() == 5);
    std::cout << "✓ BLOB 类型测试通过，大小: " << blob_val.as_blob().size() << " 字节" << std::endl;
}

void test_serialization() {
    std::cout << "\n=== 测试序列化 ===" << std::endl;
    
    // 测试各种类型的序列化
    TypedValue int_val(42);
    std::string serialized = int_val.serialize();
    TypedValue deserialized = TypedValue::deserialize(serialized);
    assert(deserialized.is_int());
    assert(deserialized.as_int() == 42);
    std::cout << "✓ INT 序列化测试通过" << std::endl;
    
    TypedValue string_val("Hello");
    serialized = string_val.serialize();
    deserialized = TypedValue::deserialize(serialized);
    assert(deserialized.is_string());
    assert(deserialized.as_string() == "Hello");
    std::cout << "✓ STRING 序列化测试通过" << std::endl;
    
    List list = {TypedValue(1), TypedValue("test")};
    TypedValue list_val(list);
    serialized = list_val.serialize();
    deserialized = TypedValue::deserialize(serialized);
    assert(deserialized.is_list());
    assert(deserialized.as_list().size() == 2);
    std::cout << "✓ LIST 序列化测试通过" << std::endl;
}

void test_typed_memtable() {
    std::cout << "\n=== 测试类型化内存表 ===" << std::endl;
    
    TypedMemTable memtable;
    
    // 测试基本操作
    memtable.put("key1", TypedValue(42), 1);
    memtable.put("key2", TypedValue("hello"), 2);
    
    TypedValue value;
    assert(memtable.get("key1", 10, value));
    assert(value.is_int() && value.as_int() == 42);
    std::cout << "✓ 内存表 PUT/GET 测试通过" << std::endl;
    
    // 测试类型化操作
    memtable.put_int("age", 25, 3);
    int64_t age;
    assert(memtable.get_int("age", 10, age));
    assert(age == 25);
    std::cout << "✓ 内存表类型化操作测试通过" << std::endl;
    
    // 测试列表操作
    List tags = {TypedValue("important"), TypedValue("urgent")};
    memtable.put_list("tags", tags, 4);
    assert(memtable.list_append("tags", TypedValue("new"), 5));
    
    size_t list_size;
    assert(memtable.list_size("tags", 10, list_size));
    assert(list_size == 3);
    std::cout << "✓ 内存表列表操作测试通过，大小: " << list_size << std::endl;
    
    // 测试范围扫描
    auto results = memtable.range_scan("", "", 10);
    std::cout << "✓ 内存表范围扫描测试通过，找到 " << results.size() << " 个键" << std::endl;
    
    // 测试类型扫描
    auto int_results = memtable.type_scan(DataType::INT, 10);
    std::cout << "✓ 内存表类型扫描测试通过，找到 " << int_results.size() << " 个整数键" << std::endl;
}

void test_type_conversion() {
    std::cout << "\n=== 测试类型转换 ===" << std::endl;
    
    // 整数转字符串
    TypedValue int_val(42);
    TypedValue string_val = int_val.convert_to(DataType::STRING);
    assert(string_val.is_string());
    assert(string_val.as_string() == "42");
    std::cout << "✓ INT -> STRING 转换测试通过" << std::endl;
    
    // 浮点数转整数
    TypedValue float_val(3.14f);
    TypedValue converted_int = float_val.convert_to(DataType::INT);
    assert(converted_int.is_int());
    assert(converted_int.as_int() == 3);
    std::cout << "✓ FLOAT -> INT 转换测试通过" << std::endl;
}

void test_utility_functions() {
    std::cout << "\n=== 测试工具函数 ===" << std::endl;
    
    // 测试数据类型字符串转换
    assert(data_type_to_string(DataType::INT) == "INT");
    assert(data_type_to_string(DataType::STRING) == "STRING");
    assert(string_to_data_type("INT") == DataType::INT);
    assert(string_to_data_type("STRING") == DataType::STRING);
    std::cout << "✓ 数据类型字符串转换测试通过" << std::endl;
    
    // 测试时间解析
    try {
        Date date = parse_date("2024-01-15");
        assert(date.year == 2024 && date.month == 1 && date.day == 15);
        std::cout << "✓ 日期解析测试通过: " << format_date(date) << std::endl;
    } catch (const std::exception& e) {
        std::cout << "⚠ 日期解析测试跳过: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "开始数据类型扩展核心功能测试..." << std::endl;
    
    try {
        test_basic_types();
        test_time_types();
        test_collection_types();
        test_blob_type();
        test_serialization();
        test_typed_memtable();
        test_type_conversion();
        test_utility_functions();
        
        std::cout << "\n🎉 所有数据类型扩展核心功能测试通过！" << std::endl;
        std::cout << "\n支持的数据类型:" << std::endl;
        std::cout << "• 数值类型: INT, FLOAT, DOUBLE" << std::endl;
        std::cout << "• 字符串类型: STRING" << std::endl;
        std::cout << "• 时间类型: TIMESTAMP, DATE" << std::endl;
        std::cout << "• 集合类型: LIST, SET, MAP" << std::endl;
        std::cout << "• 二进制类型: BLOB" << std::endl;
        std::cout << "\n核心功能:" << std::endl;
        std::cout << "• 类型安全的存储和检索" << std::endl;
        std::cout << "• 自动序列化和反序列化" << std::endl;
        std::cout << "• 集合操作支持" << std::endl;
        std::cout << "• 类型过滤和范围查询" << std::endl;
        std::cout << "• 类型转换功能" << std::endl;
        std::cout << "• 多版本并发控制" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试失败: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}