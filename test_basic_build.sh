#!/bin/bash

echo "=== Testing Basic KVDB Build Without Network ==="

# Temporarily disable network support
sed -i 's/option(ENABLE_NETWORK "Enable network interfaces (gRPC and WebSocket)" ON)/option(ENABLE_NETWORK "Enable network interfaces (gRPC and WebSocket)" OFF)/' CMakeLists.txt

# Clean and rebuild
rm -rf build
mkdir build
cd build
cmake ..
make

# Test basic functionality
echo "PUT test hello" | ./kvdb
echo "GET test" | ./kvdb

echo "=== Basic build test complete ==="