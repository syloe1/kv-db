#!/bin/bash

echo "构建基础序列化测试..."

# 创建构建目录
mkdir -p build

# 编译测试程序（包含 Binary 和 JSON 序列化）
g++ -std=c++17 -O2 -I. \
    test_serialization_basic.cpp \
    src/storage/data_types.cpp \
    src/storage/binary_serializer.cpp \
    src/storage/json_serializer.cpp \
    -o build/test_serialization_basic \
    -lz \
    -pthread

if [ $? -eq 0 ]; then
    echo "编译成功!"
    echo ""
    echo "运行序列化测试..."
    echo "=================="
    ./build/test_serialization_basic
else
    echo "编译失败!"
    exit 1
fi