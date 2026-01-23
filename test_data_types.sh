#!/bin/bash

echo "=== 数据类型扩展测试 ==="

# 清理之前的构建
rm -f test_data_types test_types.wal

# 编译测试程序
echo "编译数据类型扩展测试..."
g++ -std=c++17 -I. -O2 \
    test_data_types.cpp \
    src/storage/data_types.cpp \
    src/storage/typed_memtable.cpp \
    src/db/typed_kv_db.cpp \
    src/db/kv_db.cpp \
    src/storage/memtable.cpp \
    src/log/wal.cpp \
    src/cache/cache_manager.cpp \
    src/cache/multi_level_cache.cpp \
    src/cache/block_cache.cpp \
    src/sstable/sstable_writer.cpp \
    src/sstable/sstable_reader.cpp \
    src/sstable/block_index.cpp \
    src/sstable/sstable_meta_util.cpp \
    src/iterator/memtable_iterator.cpp \
    src/iterator/sstable_iterator.cpp \
    src/iterator/merge_iterator.cpp \
    src/iterator/concurrent_iterator.cpp \
    src/version/version_set.cpp \
    src/snapshot/snapshot_manager.cpp \
    src/compaction/compaction_strategy.cpp \
    src/compaction/compactor.cpp \
    src/index/index_manager.cpp \
    src/index/secondary_index.cpp \
    src/index/composite_index.cpp \
    src/index/fulltext_index.cpp \
    src/index/inverted_index.cpp \
    src/index/tokenizer.cpp \
    src/index/query_optimizer.cpp \
    -ljsoncpp \
    -o test_data_types

if [ $? -ne 0 ]; then
    echo "❌ 编译失败"
    exit 1
fi

echo "✅ 编译成功"

# 运行测试
echo ""
echo "运行数据类型扩展测试..."
./test_data_types

if [ $? -eq 0 ]; then
    echo ""
    echo "🎉 数据类型扩展测试全部通过！"
    
    # 显示生成的文件
    echo ""
    echo "生成的文件:"
    ls -la test_types.wal 2>/dev/null && echo "✓ WAL 文件: test_types.wal"
    
    echo ""
    echo "=== 数据类型扩展功能总结 ==="
    echo ""
    echo "📊 支持的数据类型:"
    echo "  • INT      - 64位整数"
    echo "  • FLOAT    - 32位浮点数"
    echo "  • DOUBLE   - 64位双精度浮点数"
    echo "  • STRING   - 字符串"
    echo "  • TIMESTAMP- 时间戳"
    echo "  • DATE     - 日期"
    echo "  • LIST     - 列表"
    echo "  • SET      - 集合"
    echo "  • MAP      - 映射"
    echo "  • BLOB     - 二进制数据"
    echo ""
    echo "🔧 核心功能:"
    echo "  • 类型安全的存储和检索"
    echo "  • 自动序列化和反序列化"
    echo "  • 集合操作 (append, remove, contains 等)"
    echo "  • 类型过滤查询"
    echo "  • 数值和时间范围查询"
    echo "  • 事务支持"
    echo "  • 批量操作"
    echo "  • 类型转换"
    echo "  • 统计信息"
    echo ""
    echo "💡 使用示例:"
    echo "  TypedKVDB db(\"data.wal\");"
    echo "  db.put_int(\"age\", 25);"
    echo "  db.put_timestamp_str(\"created\", \"2024-01-15 10:30:00\");"
    echo "  db.list_append(\"tags\", TypedValue(\"important\"));"
    echo "  auto results = db.type_scan(DataType::INT);"
    echo ""
    
else
    echo "❌ 数据类型扩展测试失败"
    exit 1
fi