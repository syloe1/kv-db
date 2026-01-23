#!/bin/bash

echo "=========================================="
echo "    网络分区处理系统简单测试"
echo "=========================================="

# 设置编译选项
CXX_FLAGS="-std=c++17 -Wall -Wextra -O2 -pthread"
INCLUDE_DIRS="-I. -Isrc"
LIBS="-pthread"

# 创建构建目录
mkdir -p build

echo "编译网络分区处理系统..."

# 编译分区恢复上下文
echo "编译 PartitionRecoveryContext..."
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/partition_recovery/partition_recovery_context.cpp -o build/partition_recovery_context.o
if [ $? -ne 0 ]; then
    echo "❌ PartitionRecoveryContext 编译失败"
    exit 1
fi

# 编译故障检测器实现
echo "编译 FailureDetector..."
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/partition_recovery/detector_impl.cpp -o build/detector_impl.o
if [ $? -ne 0 ]; then
    echo "❌ FailureDetector 编译失败"
    exit 1
fi

# 编译简单分区网络
echo "编译 SimplePartitionRecoveryNetwork..."
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/partition_recovery/simple_partition_network.cpp -o build/simple_partition_network.o
if [ $? -ne 0 ]; then
    echo "❌ SimplePartitionRecoveryNetwork 编译失败"
    exit 1
fi

# 编译简单测试程序
echo "编译简单测试程序..."
g++ $CXX_FLAGS $INCLUDE_DIRS -c test_partition_simple.cpp -o build/test_partition_simple.o
if [ $? -ne 0 ]; then
    echo "❌ 简单测试程序编译失败"
    exit 1
fi

# 链接生成可执行文件
echo "链接生成可执行文件..."
g++ $CXX_FLAGS -o test_partition_simple \
    build/partition_recovery_context.o \
    build/detector_impl.o \
    build/simple_partition_network.o \
    build/test_partition_simple.o \
    $LIBS

if [ $? -ne 0 ]; then
    echo "❌ 链接失败"
    exit 1
fi

echo "✅ 编译成功！"
echo ""

# 运行测试
echo "运行网络分区处理简单测试..."
echo "=========================================="
./test_partition_simple

# 检查测试结果
if [ $? -eq 0 ]; then
    echo ""
    echo "✅ 网络分区处理简单测试通过！"
    echo ""
    echo "测试验证了："
    echo "  ✓ 故障检测器创建和配置"
    echo "  ✓ 网络接口功能"
    echo "  ✓ 节点监控和健康状态"
    echo "  ✓ 网络分区模拟"
    echo "  ✓ 连通性测试"
    echo "  ✓ 分区恢复上下文"
    echo "  ✓ 一致性冲突处理"
    echo "  ✓ 统计信息收集"
    echo ""
    echo "网络分区处理系统核心功能验证完成！"
else
    echo ""
    echo "❌ 网络分区处理简单测试失败"
    exit 1
fi

echo "=========================================="
echo "    简单测试完成"
echo "=========================================="