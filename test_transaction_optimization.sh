#!/bin/bash

echo "=== 编译事务优化测试 ==="

# 创建构建目录
mkdir -p build

# 编译事务优化测试
echo "编译事务优化测试程序..."
g++ -std=c++17 -pthread -O2 \
    -I. \
    test_transaction_optimization.cpp \
    src/transaction/transaction.cpp \
    src/transaction/lock_manager.cpp \
    src/mvcc/mvcc_manager.cpp \
    -o build/test_transaction_optimization

if [ $? -eq 0 ]; then
    echo "编译成功！"
    
    echo ""
    echo "=== 运行事务优化测试 ==="
    ./build/test_transaction_optimization
    
    echo ""
    echo "=== 测试完成 ==="
else
    echo "编译失败！"
    exit 1
fi