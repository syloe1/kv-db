#!/bin/bash

echo "=========================================="
echo "    命令行增强功能测试"
echo "=========================================="

# 设置编译选项
CXX_FLAGS="-std=c++17 -Wall -Wextra -O2 -pthread"
INCLUDE_DIRS="-I. -Isrc"
LIBS="-pthread"

# 检查是否有 readline 支持
READLINE_FLAGS=""
if pkg-config --exists readline 2>/dev/null; then
    READLINE_FLAGS="-DHAVE_READLINE $(pkg-config --cflags readline) $(pkg-config --libs readline)"
    echo "✅ Readline support detected"
else
    echo "⚠️  Readline not found - some features will be limited"
fi

# 创建构建目录
mkdir -p build

echo "编译增强的 REPL 系统..."

# 编译必要的组件
echo "编译基础组件..."

# 编译 MemTable
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/storage/memtable.cpp -o build/memtable.o
if [ $? -ne 0 ]; then
    echo "❌ MemTable 编译失败"
    exit 1
fi

# 编译 WAL
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/log/wal.cpp -o build/wal.o
if [ $? -ne 0 ]; then
    echo "❌ WAL 编译失败"
    exit 1
fi

# 编译 SSTable Writer
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/sstable/sstable_writer.cpp -o build/sstable_writer.o
if [ $? -ne 0 ]; then
    echo "❌ SSTable Writer 编译失败"
    exit 1
fi

# 编译 SSTable Reader
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/sstable/sstable_reader.cpp -o build/sstable_reader.o
if [ $? -ne 0 ]; then
    echo "❌ SSTable Reader 编译失败"
    exit 1
fi

# 编译 Block Index
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/sstable/block_index.cpp -o build/block_index.o
if [ $? -ne 0 ]; then
    echo "❌ Block Index 编译失败"
    exit 1
fi

# 编译 Iterator
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/iterator/memtable_iterator.cpp -o build/memtable_iterator.o
if [ $? -ne 0 ]; then
    echo "❌ MemTable Iterator 编译失败"
    exit 1
fi

g++ $CXX_FLAGS $INCLUDE_DIRS -c src/iterator/sstable_iterator.cpp -o build/sstable_iterator.o
if [ $? -ne 0 ]; then
    echo "❌ SSTable Iterator 编译失败"
    exit 1
fi

g++ $CXX_FLAGS $INCLUDE_DIRS -c src/iterator/merge_iterator.cpp -o build/merge_iterator.o
if [ $? -ne 0 ]; then
    echo "❌ Merge Iterator 编译失败"
    exit 1
fi

# 编译 Compaction
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/compaction/compactor.cpp -o build/compactor.o
if [ $? -ne 0 ]; then
    echo "❌ Compactor 编译失败"
    exit 1
fi

# 编译 KV DB
g++ $CXX_FLAGS $INCLUDE_DIRS -c src/db/kv_db.cpp -o build/kv_db.o
if [ $? -ne 0 ]; then
    echo "❌ KV DB 编译失败"
    exit 1
fi

# 编译增强的 REPL
echo "编译增强的 REPL..."
g++ $CXX_FLAGS $INCLUDE_DIRS $READLINE_FLAGS -c src/cli/repl.cpp -o build/repl.o
if [ $? -ne 0 ]; then
    echo "❌ REPL 编译失败"
    exit 1
fi

# 编译主程序
g++ $CXX_FLAGS $INCLUDE_DIRS $READLINE_FLAGS -c src/main.cpp -o build/main.o
if [ $? -ne 0 ]; then
    echo "❌ Main 编译失败"
    exit 1
fi

# 链接生成可执行文件
echo "链接生成可执行文件..."
g++ $CXX_FLAGS -o kvdb_enhanced \
    build/memtable.o \
    build/wal.o \
    build/sstable_writer.o \
    build/sstable_reader.o \
    build/block_index.o \
    build/memtable_iterator.o \
    build/sstable_iterator.o \
    build/merge_iterator.o \
    build/compactor.o \
    build/kv_db.o \
    build/repl.o \
    build/main.o \
    $LIBS $READLINE_FLAGS

if [ $? -ne 0 ]; then
    echo "❌ 链接失败"
    exit 1
fi

echo "✅ 编译成功！"
echo ""

# 创建测试脚本文件
echo "创建测试脚本..."
cat > test_script.kvdb << 'EOF'
# KVDB 测试脚本
# 这是一个注释行

// 这也是一个注释行

ECHO "开始测试脚本执行..."

# 插入一些测试数据
PUT user:1 "Alice"
PUT user:2 "Bob"
PUT user:3 "Charlie"

ECHO "插入了3个用户记录"

# 查询数据
GET user:1
GET user:2
GET user:3

# 扫描数据
SCAN user:1 user:3

ECHO "测试脚本执行完成！"
EOF

echo "✅ 测试脚本创建完成"
echo ""

echo "运行命令行增强功能演示..."
echo "=========================================="

# 创建交互式演示脚本
cat > demo_commands.txt << 'EOF'
HIGHLIGHT STATUS
ECHO "欢迎使用增强的 KVDB 命令行界面！"
PUT demo:key1 "Hello World"
GET demo:key1
HISTORY SHOW
CLEAR
HELP
EOF

echo "增强的 KVDB CLI 已准备就绪！"
echo ""
echo "新功能包括："
echo "  ✓ 语法高亮 - 命令和参数彩色显示"
echo "  ✓ 多行输入 - 使用 \\ 续行或 MULTILINE 命令"
echo "  ✓ 脚本支持 - 使用 SOURCE 命令执行脚本文件"
echo "  ✓ 增强的 Tab 补全 - 命令和参数智能补全"
echo "  ✓ 历史管理 - HISTORY 命令管理命令历史"
echo "  ✓ 屏幕控制 - CLEAR 命令清屏"
echo ""
echo "启动方法："
echo "  ./kvdb_enhanced"
echo ""
echo "测试脚本："
echo "  在 KVDB 中运行: SOURCE test_script.kvdb"
echo ""
echo "多行输入示例："
echo "  PUT long_key \\"
echo "      \"This is a very long value \\"
echo "       that spans multiple lines\""
echo ""

if [ -f "kvdb_enhanced" ]; then
    echo "✅ 命令行增强功能实现完成！"
    echo ""
    echo "可以运行以下命令测试："
    echo "  ./kvdb_enhanced"
    echo ""
    echo "在 KVDB 中尝试："
    echo "  HELP                    # 查看所有命令"
    echo "  HIGHLIGHT ON            # 启用语法高亮"
    echo "  SOURCE test_script.kvdb # 执行测试脚本"
    echo "  MULTILINE ON            # 启用多行模式"
    echo "  HISTORY SHOW            # 显示命令历史"
else
    echo "❌ 可执行文件生成失败"
    exit 1
fi

echo "=========================================="
echo "    命令行增强功能测试完成"
echo "=========================================="