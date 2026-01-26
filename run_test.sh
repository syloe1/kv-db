#!/bin/bash

# 运行KVDB测试并将结果保存到文件

echo "开始运行KVDB功能测试..."

cd build

# 运行测试并将输出保存到文件
echo "SOURCE ../simple_test.kvdb
EXIT" | ./kvdb > ../test_results.txt 2>&1

echo "测试完成，结果已保存到 test_results.txt"
echo "请查看 test_results.txt 文件内容"