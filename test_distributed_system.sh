#!/bin/bash

echo "Building Distributed System Test..."

# 编译测试程序
g++ -std=c++17 -O2 -I. \
    test_distributed_system.cpp \
    src/distributed/distributed_kvdb.cpp \
    src/distributed/shard_manager.cpp \
    src/distributed/load_balancer.cpp \
    src/distributed/failover_manager.cpp \
    src/db/kv_db.cpp \
    src/storage/memtable.cpp \
    src/sstable/sstable_writer.cpp \
    src/sstable/sstable_reader.cpp \
    src/sstable/block_index.cpp \
    src/log/wal.cpp \
    src/version/version_set.cpp \
    -lpthread \
    -o test_distributed_system

if [ $? -eq 0 ]; then
    echo "Build successful! Running tests..."
    echo "=================================="
    ./test_distributed_system
    
    if [ $? -eq 0 ]; then
        echo "=================================="
        echo "All distributed system tests passed!"
    else
        echo "Some tests failed!"
        exit 1
    fi
else
    echo "Build failed!"
    exit 1
fi