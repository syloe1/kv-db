#!/bin/bash

echo "KVDB 索引系统简化测试脚本"
echo "========================="

# 清理之前的构建
echo "清理之前的构建文件..."
rm -f test_index_simple

# 编译简化测试程序
echo "编译简化测试程序..."
g++ -std=c++17 -O2 -Isrc \
    test_index_simple.cpp \
    src/index/secondary_index.cpp \
    src/index/composite_index.cpp \
    src/index/tokenizer.cpp \
    src/index/fulltext_index.cpp \
    src/index/inverted_index.cpp \
    -o test_index_simple \
    -pthread

if [ $? -eq 0 ]; then
    echo "编译成功！"
    echo ""
    
    # 运行测试
    echo "运行索引系统测试..."
    echo "===================="
    ./test_index_simple
    
    echo ""
    echo "测试完成！"
    
else
    echo "编译失败！请检查错误信息。"
    exit 1
fi