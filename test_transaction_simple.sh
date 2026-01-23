#!/bin/bash

echo "=== 编译简化事务测试 ==="

# 创建构建目录
mkdir -p build

# 编译简化事务测试
echo "编译简化事务测试程序..."
g++ -std=c++17 -pthread -O2 \
    -I. \
    test_transaction_simple.cpp \
    src/transaction/transaction.cpp \
    src/transaction/lock_manager.cpp \
    src/mvcc/mvcc_manager.cpp \
    -o build/test_transaction_simple

if [ $? -eq 0 ]; then
    echo "编译成功！"
    
    echo ""
    echo "=== 运行简化事务测试 ==="
    ./build/test_transaction_simple
    
    echo ""
    echo "=== 测试完成 ==="
else
    echo "编译失败！"
    exit 1
fi