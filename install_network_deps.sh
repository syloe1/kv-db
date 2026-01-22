#!/bin/bash

echo "=== Installing KVDB Network Dependencies ==="
echo

# 更新包列表
echo "1. Updating package list..."
sudo apt-get update

# 安装基础依赖
echo "2. Installing basic dependencies..."
sudo apt-get install -y pkg-config build-essential cmake

# 安装 gRPC 和 Protocol Buffers
echo "3. Installing gRPC and Protocol Buffers..."
sudo apt-get install -y libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc

# 安装 WebSocket++ 和 JsonCpp
echo "4. Installing WebSocket++ and JsonCpp..."
sudo apt-get install -y libwebsocketpp-dev libjsoncpp-dev

# 安装 Python 依赖（用于测试客户端）
echo "5. Installing Python dependencies..."
pip3 install websockets asyncio

echo
echo "=== Dependencies Installation Complete ==="
echo "You can now rebuild KVDB with network support:"
echo "  rm -rf build && mkdir build && cd build && cmake .. && make"
echo