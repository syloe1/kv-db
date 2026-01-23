#!/bin/bash

echo "========================================"
echo "        Raft实现测试脚本"
echo "========================================"

# 检查构建环境
echo "[INFO] 检查构建环境..."
if ! command -v g++ &> /dev/null; then
    echo "[ERROR] g++编译器未找到"
    exit 1
fi

echo "[SUCCESS] 构建环境检查通过"

# 清理之前的构建文件
echo "[INFO] 清理构建文件..."
rm -f test_raft_implementation
rm -f *.o

# 编译Raft实现
echo "[INFO] 编译Raft实现..."

# 编译源文件
echo "编译 raft_node.cpp..."
g++ -c src/raft/raft_node.cpp -o raft_node.o -std=c++17 -I. -pthread

if [ $? -ne 0 ]; then
    echo "[ERROR] raft_node.cpp编译失败"
    exit 1
fi

echo "编译 simple_raft_network.cpp..."
g++ -c src/raft/simple_raft_network.cpp -o simple_raft_network.o -std=c++17 -I. -pthread

if [ $? -ne 0 ]; then
    echo "[ERROR] simple_raft_network.cpp编译失败"
    exit 1
fi

echo "编译 simple_raft_state_machine.cpp..."
g++ -c src/raft/simple_raft_state_machine.cpp -o simple_raft_state_machine.o -std=c++17 -I. -pthread

if [ $? -ne 0 ]; then
    echo "[ERROR] simple_raft_state_machine.cpp编译失败"
    exit 1
fi

echo "编译 test_raft_implementation.cpp..."
g++ -c test_raft_implementation.cpp -o test_raft_implementation.o -std=c++17 -I. -pthread

if [ $? -ne 0 ]; then
    echo "[ERROR] test_raft_implementation.cpp编译失败"
    exit 1
fi

# 链接生成可执行文件
echo "链接生成可执行文件..."
g++ raft_node.o simple_raft_network.o simple_raft_state_machine.o test_raft_implementation.o \
    -o test_raft_implementation -std=c++17 -pthread

if [ $? -ne 0 ]; then
    echo "[ERROR] 链接失败"
    exit 1
fi

echo "[SUCCESS] Raft实现编译成功"

# 运行测试
echo ""
echo "[INFO] 运行Raft实现测试..."
echo ""

./test_raft_implementation

if [ $? -eq 0 ]; then
    echo ""
    echo "[SUCCESS] Raft实现测试通过"
else
    echo ""
    echo "[ERROR] Raft实现测试失败"
    exit 1
fi

# 清理编译文件
echo ""
echo "[INFO] 清理编译文件..."
rm -f *.o

echo ""
echo "========================================"
echo "           测试完成"
echo "========================================"
echo ""
echo "💡 提示:"
echo "- Raft分布式共识协议实现完成"
echo "- 支持领导者选举、日志复制、客户端请求处理"
echo "- 包含网络层和状态机的简单实现"
echo "- 测试了3节点集群的基本功能"
echo ""
echo "🎯 实现的核心功能:"
echo "✓ 领导者选举算法"
echo "✓ 日志复制机制"
echo "✓ 客户端请求处理"
echo "✓ 网络通信抽象"
echo "✓ 状态机接口"
echo "✓ 集群管理"
echo ""
echo "📈 下一步可以实现:"
echo "- 持久化存储"
echo "- 快照机制"
echo "- 网络分区处理"
echo "- 性能优化"
echo "- 真实网络通信"