#!/bin/bash

echo "=== KVDB 网络接口测试 ==="
echo

# 检查依赖
echo "1. 检查依赖..."
if ! command -v python3 &> /dev/null; then
    echo "Error: Python3 not found"
    exit 1
fi

if ! python3 -c "import websockets, json" 2>/dev/null; then
    echo "Installing websockets..."
    pip3 install websockets
fi

# 启动 KVDB 服务器（后台）
echo "2. 启动 KVDB 服务器..."
{
    echo "START_NETWORK all"
    echo "PUT test:welcome 'Hello Network Interface!'"
    echo "GET test:welcome"
    sleep 30  # 保持服务器运行30秒
    echo "STOP_NETWORK all"
    echo "EXIT"
} | ./build/kvdb &

KVDB_PID=$!
echo "KVDB server started with PID: $KVDB_PID"

# 等待服务器启动
echo "3. 等待服务器启动..."
sleep 3

# 测试 WebSocket 接口
echo "4. 测试 WebSocket 接口..."

echo "  - 基本操作测试"
python3 test_network_client.py basic

echo "  - 批量操作测试"
python3 test_network_client.py batch

echo "  - 扫描操作测试"
python3 test_network_client.py scan

echo "  - 快照操作测试"
python3 test_network_client.py snapshot

echo "  - 管理操作测试"
python3 test_network_client.py management

# 性能测试（可选）
if [ "$1" = "performance" ]; then
    echo "  - 性能测试"
    python3 test_network_client.py performance
fi

# 清理
echo "5. 清理..."
kill $KVDB_PID 2>/dev/null || true
wait $KVDB_PID 2>/dev/null || true

echo
echo "=== 网络接口测试完成 ==="