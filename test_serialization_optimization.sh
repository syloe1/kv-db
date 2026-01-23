#!/bin/bash

echo "构建序列化优化测试..."

# 创建构建目录
mkdir -p build

# 编译测试程序
g++ -std=c++17 -O2 -I. \
    test_serialization_optimization.cpp \
    -o build/test_serialization_optimization \
    -lz \
    -pthread

if [ $? -eq 0 ]; then
    echo "编译成功!"
    echo ""
    echo "运行序列化优化测试..."
    echo "========================"
    ./build/test_serialization_optimization
else
    echo "编译失败!"
    exit 1
fi