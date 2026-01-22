#!/bin/bash

echo "=== 测试 KVDB 帮助系统 ==="
echo

echo "1. 测试基本 HELP 命令："
echo "HELP" | ./build/kvdb
echo

echo "2. 测试 MAN 命令："
echo -e "MAN PUT\nEXIT" | ./build/kvdb
echo

echo "3. 测试 COMMAND HELP 格式："
echo -e "GET HELP\nEXIT" | ./build/kvdb
echo

echo "4. 测试 COMMAND - HELP 格式："
echo -e "SCAN - HELP\nEXIT" | ./build/kvdb
echo

echo "5. 测试无效命令的 MAN："
echo -e "MAN INVALID\nEXIT" | ./build/kvdb
echo

echo "=== 测试完成 ==="