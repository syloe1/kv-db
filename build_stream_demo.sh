#!/bin/bash

echo "=== 构建流式处理演示程序 ==="

# 编译演示程序
g++ -std=c++17 -O2 -I. \
    demo_stream_processing.cpp \
    src/stream/change_stream.cpp \
    src/stream/realtime_sync.cpp \
    src/stream/event_driven.cpp \
    src/stream/stream_computing.cpp \
    -lpthread \
    -o demo_stream_processing

if [ $? -eq 0 ]; then
    echo "✅ 构建成功!"
    echo ""
    echo "=== 运行流式处理演示 ==="
    ./demo_stream_processing
    
    if [ $? -eq 0 ]; then
        echo ""
        echo "🎉 演示程序运行完成!"
    else
        echo ""
        echo "❌ 演示程序运行失败!"
        exit 1
    fi
else
    echo "❌ 构建失败!"
    exit 1
fi