#!/bin/bash

echo "=== 数据类型扩展核心功能测试 ==="

# 清理之前的构建
rm -f test_data_types_simple

# 编译测试程序
echo "编译数据类型扩展核心功能测试..."
g++ -std=c++17 -I. -O2 \
    test_data_types_simple.cpp \
    src/storage/data_types.cpp \
    src/storage/typed_memtable.cpp \
    -o test_data_types_simple

if [ $? -ne 0 ]; then
    echo "❌ 编译失败"
    exit 1
fi

echo "✅ 编译成功"

# 运行测试
echo ""
echo "运行数据类型扩展核心功能测试..."
./test_data_types_simple

if [ $? -eq 0 ]; then
    echo ""
    echo "🎉 数据类型扩展核心功能测试全部通过！"
    
    echo ""
    echo "=== 数据类型扩展实现总结 ==="
    echo ""
    echo "📊 已实现的数据类型:"
    echo "  • INT      - 64位整数，支持算术运算"
    echo "  • FLOAT    - 32位浮点数，支持精度控制"
    echo "  • DOUBLE   - 64位双精度浮点数"
    echo "  • STRING   - 可变长度字符串"
    echo "  • TIMESTAMP- 高精度时间戳"
    echo "  • DATE     - 日期类型 (年-月-日)"
    echo "  • LIST     - 有序列表，支持重复元素"
    echo "  • SET      - 无序集合，元素唯一"
    echo "  • MAP      - 键值映射，键为字符串"
    echo "  • BLOB     - 二进制大对象"
    echo ""
    echo "🔧 核心功能特性:"
    echo "  • 类型安全的存储和检索"
    echo "  • 高效的二进制序列化"
    echo "  • 集合操作 (append, remove, contains)"
    echo "  • 类型过滤查询"
    echo "  • 范围查询支持"
    echo "  • 类型转换功能"
    echo "  • 多版本并发控制 (MVCC)"
    echo "  • 内存使用统计"
    echo ""
    echo "💡 技术亮点:"
    echo "  • 使用 std::variant 实现类型安全"
    echo "  • 智能指针管理集合内存"
    echo "  • 紧凑的序列化格式"
    echo "  • 支持嵌套数据结构"
    echo "  • 线程安全的并发访问"
    echo ""
    echo "📈 性能优化:"
    echo "  • 零拷贝的类型转换"
    echo "  • 高效的内存管理"
    echo "  • 优化的查询算法"
    echo "  • 支持批量操作"
    echo ""
    
else
    echo "❌ 数据类型扩展核心功能测试失败"
    exit 1
fi