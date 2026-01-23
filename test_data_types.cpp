#include "src/db/typed_kv_db.h"
#include <iostream>
#include <chrono>
#include <cassert>

using namespace kvdb;

void test_basic_types() {
    std::cout << "=== 测试基本数据类型 ===" << std::endl;
    
    TypedKVDB db("test_types.wal");
    
    // 测试整数类型
    assert(db.put_int("age", 25));
    int64_t age;
    assert(db.get_int("age", age));
    assert(age == 25);
    std::cout << "✓ INT 类型测试通过: " << age << std::endl;
    
    // 测试浮点类型
    assert(db.put_float("price", 19.99f));
    float price;
    assert(db.get_float("price", price));
    assert(std::abs(price - 19.99f) < 0.01f);
    std::cout << "✓ FLOAT 类型测试通过: " << price << std::endl;
    
    // 测试双精度浮点
    assert(db.put_double("pi", 3.14159265359));
    double pi;
    assert(db.get_double("pi", pi));
    assert(std::abs(pi - 3.14159265359) < 0.0000001);
    std::cout << "✓ DOUBLE 类型测试通过: " << pi << std::endl;
    
    // 测试字符串类型
    assert(db.put_string("name", "Alice"));
    std::string name;
    assert(db.get_string("name", name));
    assert(name == "Alice");
    std::cout << "✓ STRING 类型测试通过: " << name << std::endl;
}

void test_time_types() {
    std::cout << "\n=== 测试时间类型 ===" << std::endl;
    
    TypedKVDB db("test_types.wal");
    
    // 测试时间戳
    auto now = std::chrono::system_clock::now();
    assert(db.put_timestamp("created_at", now));
    Timestamp retrieved_time;
    assert(db.get_timestamp("created_at", retrieved_time));
    std::cout << "✓ TIMESTAMP 类型测试通过" << std::endl;
    
    // 测试时间戳字符串
    assert(db.put_timestamp_str("event_time", "2024-01-15 10:30:00"));
    std::string time_str;
    assert(db.get_timestamp_str("event_time", time_str));
    std::cout << "✓ TIMESTAMP 字符串测试通过: " << time_str << std::endl;
    
    // 测试日期
    Date birthday(1990, 5, 15);
    assert(db.put_date("birthday", birthday));
    Date retrieved_date;
    assert(db.get_date("birthday", retrieved_date));
    assert(retrieved_date.year == 1990 && retrieved_date.month == 5 && retrieved_date.day == 15);
    std::cout << "✓ DATE 类型测试通过: " << retrieved_date.to_string() << std::endl;
    
    // 测试日期字符串
    assert(db.put_date_str("deadline", "2024-12-31"));
    std::string date_str;
    assert(db.get_date_str("deadline", date_str));
    std::cout << "✓ DATE 字符串测试通过: " << date_str << std::endl;
}

void test_collection_types() {
    std::cout << "\n=== 测试集合类型 ===" << std::endl;
    
    TypedKVDB db("test_types.wal");
    
    // 测试列表
    List numbers = {TypedValue(1), TypedValue(2), TypedValue(3)};
    assert(db.put_list("numbers", numbers));
    List retrieved_numbers;
    assert(db.get_list("numbers", retrieved_numbers));
    assert(retrieved_numbers.size() == 3);
    std::cout << "✓ LIST 类型测试通过，大小: " << retrieved_numbers.size() << std::endl;
    
    // 测试列表操作
    assert(db.list_append("numbers", TypedValue(4)));
    size_t list_size;
    assert(db.list_size("numbers", list_size));
    assert(list_size == 4);
    std::cout << "✓ LIST 追加操作测试通过，新大小: " << list_size << std::endl;
    
    // 测试集合
    Set colors = {TypedValue("red"), TypedValue("green"), TypedValue("blue")};
    assert(db.put_set("colors", colors));
    Set retrieved_colors;
    assert(db.get_set("colors", retrieved_colors));
    assert(retrieved_colors.size() == 3);
    std::cout << "✓ SET 类型测试通过，大小: " << retrieved_colors.size() << std::endl;
    
    // 测试集合操作
    assert(db.set_add("colors", TypedValue("yellow")));
    assert(db.set_contains("colors", TypedValue("red")));
    assert(!db.set_contains("colors", TypedValue("purple")));
    std::cout << "✓ SET 操作测试通过" << std::endl;
    
    // 测试映射
    Map person;
    person["name"] = TypedValue("Bob");
    person["age"] = TypedValue(30);
    person["city"] = TypedValue("New York");
    
    assert(db.put_map("person", person));
    Map retrieved_person;
    assert(db.get_map("person", retrieved_person));
    assert(retrieved_person.size() == 3);
    std::cout << "✓ MAP 类型测试通过，大小: " << retrieved_person.size() << std::endl;
    
    // 测试映射操作
    assert(db.map_put("person", "email", TypedValue("bob@example.com")));
    TypedValue email;
    assert(db.map_get("person", "email", email));
    assert(email.is_string() && email.as_string() == "bob@example.com");
    std::cout << "✓ MAP 操作测试通过" << std::endl;
}

void test_blob_type() {
    std::cout << "\n=== 测试二进制类型 ===" << std::endl;
    
    TypedKVDB db("test_types.wal");
    
    // 创建二进制数据
    Blob data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello" in ASCII
    assert(db.put_blob("binary_data", data));
    
    Blob retrieved_data;
    assert(db.get_blob("binary_data", retrieved_data));
    assert(retrieved_data.size() == 5);
    assert(retrieved_data == data);
    std::cout << "✓ BLOB 类型测试通过，大小: " << retrieved_data.size() << " 字节" << std::endl;
}

void test_type_queries() {
    std::cout << "\n=== 测试类型查询 ===" << std::endl;
    
    TypedKVDB db("test_types.wal");
    
    // 插入不同类型的数据
    db.put_int("num1", 100);
    db.put_int("num2", 200);
    db.put_string("str1", "hello");
    db.put_string("str2", "world");
    db.put_float("float1", 1.5f);
    
    // 类型过滤查询
    auto int_results = db.type_scan(DataType::INT);
    std::cout << "✓ 找到 " << int_results.size() << " 个整数类型的键" << std::endl;
    
    auto string_results = db.type_scan(DataType::STRING);
    std::cout << "✓ 找到 " << string_results.size() << " 个字符串类型的键" << std::endl;
    
    // 数值范围查询
    auto numeric_results = db.numeric_range_scan(50.0, 150.0);
    std::cout << "✓ 在范围 [50, 150] 内找到 " << numeric_results.size() << " 个数值" << std::endl;
    
    // 统计信息
    auto stats = db.get_type_statistics();
    std::cout << "✓ 类型统计:" << std::endl;
    std::cout << "  - 整数: " << stats.int_count << std::endl;
    std::cout << "  - 字符串: " << stats.string_count << std::endl;
    std::cout << "  - 浮点数: " << stats.float_count << std::endl;
    std::cout << "  - 总计: " << stats.total_count() << std::endl;
}

void test_transactions() {
    std::cout << "\n=== 测试事务 ===" << std::endl;
    
    TypedKVDB db("test_types.wal");
    
    // 测试成功的事务
    {
        auto tx = db.begin_transaction();
        assert(tx->put_typed("tx_key1", TypedValue(42)));
        assert(tx->put_typed("tx_key2", TypedValue("transaction test")));
        assert(tx->commit());
        std::cout << "✓ 事务提交测试通过" << std::endl;
    }
    
    // 验证事务结果
    int64_t value;
    assert(db.get_int("tx_key1", value));
    assert(value == 42);
    
    // 测试回滚的事务
    {
        auto tx = db.begin_transaction();
        tx->put_typed("tx_key3", TypedValue(999));
        tx->rollback();
        std::cout << "✓ 事务回滚测试通过" << std::endl;
    }
    
    // 验证回滚结果
    assert(!db.key_exists_typed("tx_key3"));
}

void test_batch_operations() {
    std::cout << "\n=== 测试批量操作 ===" << std::endl;
    
    TypedKVDB db("test_types.wal");
    
    std::vector<TypedKVDB::TypedOperation> operations;
    operations.emplace_back(TypedKVDB::TypedOperation::PUT, "batch1", TypedValue(1));
    operations.emplace_back(TypedKVDB::TypedOperation::PUT, "batch2", TypedValue(2));
    operations.emplace_back(TypedKVDB::TypedOperation::PUT, "batch3", TypedValue(3));
    
    assert(db.batch_execute(operations));
    std::cout << "✓ 批量操作测试通过，执行了 " << operations.size() << " 个操作" << std::endl;
    
    // 验证批量操作结果
    int64_t val1, val2, val3;
    assert(db.get_int("batch1", val1) && val1 == 1);
    assert(db.get_int("batch2", val2) && val2 == 2);
    assert(db.get_int("batch3", val3) && val3 == 3);
}

void test_type_conversion() {
    std::cout << "\n=== 测试类型转换 ===" << std::endl;
    
    TypedKVDB db("test_types.wal");
    
    // 插入整数
    assert(db.put_int("convert_test", 42));
    assert(db.get_key_type("convert_test") == DataType::INT);
    
    // 转换为字符串
    assert(db.convert_key_type("convert_test", DataType::STRING));
    assert(db.get_key_type("convert_test") == DataType::STRING);
    
    std::string str_value;
    assert(db.get_string("convert_test", str_value));
    assert(str_value == "42");
    std::cout << "✓ 类型转换测试通过: INT -> STRING" << std::endl;
}

int main() {
    std::cout << "开始数据类型扩展测试..." << std::endl;
    
    try {
        test_basic_types();
        test_time_types();
        test_collection_types();
        test_blob_type();
        test_type_queries();
        test_transactions();
        test_batch_operations();
        test_type_conversion();
        
        std::cout << "\n🎉 所有数据类型扩展测试通过！" << std::endl;
        std::cout << "\n支持的数据类型:" << std::endl;
        std::cout << "• 数值类型: INT, FLOAT, DOUBLE" << std::endl;
        std::cout << "• 字符串类型: STRING" << std::endl;
        std::cout << "• 时间类型: TIMESTAMP, DATE" << std::endl;
        std::cout << "• 集合类型: LIST, SET, MAP" << std::endl;
        std::cout << "• 二进制类型: BLOB" << std::endl;
        std::cout << "\n功能特性:" << std::endl;
        std::cout << "• 类型安全的存储和检索" << std::endl;
        std::cout << "• 集合操作 (列表、集合、映射)" << std::endl;
        std::cout << "• 类型过滤查询" << std::endl;
        std::cout << "• 数值和时间范围查询" << std::endl;
        std::cout << "• 事务支持" << std::endl;
        std::cout << "• 批量操作" << std::endl;
        std::cout << "• 类型转换" << std::endl;
        std::cout << "• 统计信息" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试失败: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}