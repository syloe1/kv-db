#!/bin/bash

echo "=== 编译事务优化演示 ==="

# 创建构建目录
mkdir -p build

# 编译事务优化演示
echo "编译事务优化演示程序..."
g++ -std=c++17 -pthread -O2 \
    -I. \
    demo_transaction_optimization.cpp \
    src/transaction/transaction.cpp \
    src/transaction/lock_manager.cpp \
    src/mvcc/mvcc_manager.cpp \
    -o build/demo_transaction_optimization

if [ $? -eq 0 ]; then
    echo "编译成功！"
    
    echo ""
    echo "=== 运行事务优化演示 ==="
    ./build/demo_transaction_optimization
    
    echo ""
    echo "=== 演示完成 ==="
else
    echo "编译失败！"
    exit 1
fi