#!/bin/bash

echo "=== Building Stream Processing Test ==="

# 编译测试程序
g++ -std=c++17 -O2 -I. \
    test_stream_processing.cpp \
    src/stream/change_stream.cpp \
    src/stream/realtime_sync.cpp \
    src/stream/event_driven.cpp \
    src/stream/stream_computing.cpp \
    -lpthread \
    -o test_stream_processing

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo ""
    echo "=== Running Stream Processing Test ==="
    ./test_stream_processing
    
    if [ $? -eq 0 ]; then
        echo ""
        echo "=== Stream Processing Test Completed Successfully ==="
    else
        echo ""
        echo "=== Stream Processing Test Failed ==="
        exit 1
    fi
else
    echo "Build failed!"
    exit 1
fi