#!/bin/bash

# KVDB SDK构建脚本
set -e

echo "Building KVDB Client SDKs..."

# 检查protobuf编译器
if ! command -v protoc &> /dev/null; then
    echo "Error: protoc not found. Please install Protocol Buffers compiler."
    exit 1
fi

# 生成protobuf代码
echo "Generating protobuf code..."
mkdir -p generated

# 为C++生成代码
protoc --cpp_out=generated --grpc_out=generated --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` ../proto/kvdb.proto

# 为Python生成代码
protoc --python_out=python/kvdb_client --grpc_python_out=python/kvdb_client ../proto/kvdb.proto

# 为Java生成代码
protoc --java_out=java/src/main/java --grpc-java_out=java/src/main/java ../proto/kvdb.proto

# 为Go生成代码
protoc --go_out=go --go-grpc_out=go ../proto/kvdb.proto

echo "Building C++ SDK..."
cd cpp
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ../..

echo "Building Python SDK..."
cd python
python3 -m pip install -e .
cd ..

echo "Building Java SDK..."
cd java
mvn clean compile
cd ..

echo "Building Go SDK..."
cd go
go mod tidy
go build ./...
cd ..

echo "All SDKs built successfully!"

# 运行测试（如果存在）
echo "Running tests..."

# C++ tests
if [ -f cpp/build/test_client ]; then
    echo "Running C++ tests..."
    cpp/build/test_client
fi

# Python tests
if [ -f python/tests/test_client.py ]; then
    echo "Running Python tests..."
    cd python
    python3 -m pytest tests/
    cd ..
fi

# Java tests
cd java
if [ -d src/test ]; then
    echo "Running Java tests..."
    mvn test
fi
cd ..

# Go tests
cd go
if [ -f *_test.go ]; then
    echo "Running Go tests..."
    go test ./...
fi
cd ..

echo "Build and test completed successfully!"