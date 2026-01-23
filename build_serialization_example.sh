#!/bin/bash

echo "构建序列化使用示例..."

# 创建构建目录
mkdir -p build

# 编译示例程序
g++ -std=c++17 -O2 -I. \
    serialization_example.cpp \
    src/storage/data_types.cpp \
    src/storage/binary_serializer.cpp \
    src/storage/json_serializer.cpp \
    -o build/serialization_example \
    -lz \
    -pthread

if [ $? -eq 0 ]; then
    echo "编译成功!"
    echo ""
    echo "运行序列化示例..."
    echo "=================="
    ./build/serialization_example
else
    echo "编译失败!"
    exit 1
fi