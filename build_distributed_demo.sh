#!/bin/bash

echo "构建分布式系统演示程序..."

# 编译演示程序
g++ -std=c++17 -O2 -I. \
    distributed_demo.cpp \
    src/distributed/shard_manager.cpp \
    src/distributed/load_balancer.cpp \
    src/distributed/failover_manager.cpp \
    -lpthread \
    -o distributed_demo

if [ $? -eq 0 ]; then
    echo "构建成功！运行演示..."
    echo "=================================="
    ./distributed_demo
    
    if [ $? -eq 0 ]; then
        echo "=================================="
        echo "演示程序运行完成！"
    else
        echo "演示程序运行失败！"
        exit 1
    fi
else
    echo "构建失败！"
    exit 1
fi