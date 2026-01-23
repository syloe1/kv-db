#!/bin/bash

echo "=== KVDB 高级查询功能测试 ==="

# 检查是否存在构建目录
if [ ! -d "build" ]; then
    echo "创建构建目录..."
    mkdir build
fi

cd build

# 构建项目
echo "构建项目..."
cmake .. -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo "❌ CMake 配置失败"
    exit 1
fi

make -j$(nproc)
if [ $? -ne 0 ]; then
    echo "❌ 编译失败"
    exit 1
fi

echo "✅ 编译成功"

# 编译测试程序
echo "编译测试程序..."
g++ -std=c++17 -I../src -O2 ../test_advanced_queries.cpp \
    ../src/db/kv_db.cpp \
    ../src/query/query_engine.cpp \
    ../src/storage/memtable.cpp \
    ../src/log/wal.cpp \
    ../src/sstable/sstable_writer.cpp \
    ../src/sstable/sstable_reader.cpp \
    ../src/sstable/sstable_meta_util.cpp \
    ../src/sstable/block_index.cpp \
    ../src/compaction/compactor.cpp \
    ../src/compaction/compaction_strategy.cpp \
    ../src/bloom/bloom_filter.cpp \
    ../src/cache/block_cache.cpp \
    ../src/version/version_set.cpp \
    ../src/snapshot/snapshot_manager.cpp \
    ../src/iterator/memtable_iterator.cpp \
    ../src/iterator/sstable_iterator.cpp \
    ../src/iterator/merge_iterator.cpp \
    ../src/iterator/concurrent_iterator.cpp \
    -lstdc++fs -o test_advanced_queries

if [ $? -ne 0 ]; then
    echo "❌ 测试程序编译失败"
    exit 1
fi

echo "✅ 测试程序编译成功"

# 运行测试
echo ""
echo "运行功能测试..."
./test_advanced_queries
if [ $? -ne 0 ]; then
    echo "❌ 功能测试失败"
    exit 1
fi

echo ""
echo "✅ 所有测试通过！"

# 运行交互式演示
echo ""
echo "启动交互式演示..."
echo "您可以运行以下命令来体验高级查询功能："
echo ""
echo "1. 启动 KVDB:"
echo "   ./kvdb"
echo ""
echo "2. 在 KVDB 中运行演示脚本:"
echo "   SOURCE ../demo_advanced_queries.kvdb"
echo ""
echo "3. 或者手动尝试以下命令:"
echo "   BATCH PUT user:1 Alice user:2 Bob user:3 Charlie"
echo "   GET_WHERE key LIKE 'user:*'"
echo "   COUNT"
echo "   SUM score:*"
echo "   SCAN_ORDER ASC"
echo ""

cd ..
echo "测试完成！"