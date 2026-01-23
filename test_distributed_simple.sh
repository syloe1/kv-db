#!/bin/bash

echo "Building Distributed Components Test..."

# 编译测试程序
g++ -std=c++17 -O2 -I. \
    test_distributed_simple.cpp \
    src/distributed/shard_manager.cpp \
    src/distributed/load_balancer.cpp \
    src/distributed/failover_manager.cpp \
    -lpthread \
    -o test_distributed_simple

if [ $? -eq 0 ]; then
    echo "Build successful! Running tests..."
    echo "=================================="
    ./test_distributed_simple
    
    if [ $? -eq 0 ]; then
        echo "=================================="
        echo "All distributed components tests passed!"
    else
        echo "Some tests failed!"
        exit 1
    fi
else
    echo "Build failed!"
    exit 1
fi