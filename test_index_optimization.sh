#!/bin/bash

echo "KVDB 索引优化测试脚本"
echo "====================="

# 清理之前的构建
echo "清理之前的构建文件..."
rm -f test_index_optimization
rm -f *.wal
rm -rf data/

# 编译测试程序
echo "编译索引优化测试程序..."
g++ -std=c++17 -O2 -Isrc \
    test_index_optimization.cpp \
    src/db/kv_db.cpp \
    src/storage/memtable.cpp \
    src/log/wal.cpp \
    src/sstable/sstable_writer.cpp \
    src/sstable/sstable_reader.cpp \
    src/sstable/sstable_meta_util.cpp \
    src/sstable/block_index.cpp \
    src/compaction/compactor.cpp \
    src/compaction/compaction_strategy.cpp \
    src/bloom/bloom_filter.cpp \
    src/cache/block_cache.cpp \
    src/cache/cache_manager.cpp \
    src/cache/multi_level_cache.cpp \
    src/version/version_set.cpp \
    src/snapshot/snapshot_manager.cpp \
    src/iterator/memtable_iterator.cpp \
    src/iterator/sstable_iterator.cpp \
    src/iterator/merge_iterator.cpp \
    src/iterator/concurrent_iterator.cpp \
    src/query/query_engine.cpp \
    src/index/secondary_index.cpp \
    src/index/composite_index.cpp \
    src/index/tokenizer.cpp \
    src/index/fulltext_index.cpp \
    src/index/inverted_index.cpp \
    src/index/index_manager.cpp \
    src/index/query_optimizer.cpp \
    -o test_index_optimization \
    -pthread -lstdc++fs

if [ $? -eq 0 ]; then
    echo "编译成功！"
    echo ""
    
    # 运行测试
    echo "运行索引优化测试..."
    echo "===================="
    ./test_index_optimization
    
    echo ""
    echo "测试完成！"
    
    # 显示生成的文件
    echo ""
    echo "生成的文件:"
    ls -la *.wal data/ 2>/dev/null || echo "没有生成WAL文件或数据目录"
    
else
    echo "编译失败！请检查错误信息。"
    exit 1
fi