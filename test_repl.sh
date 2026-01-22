#!/bin/bash
# REPL 功能测试脚本

echo "=== KVDB REPL 功能测试 ==="
echo ""

# 清理旧文件
rm -f kvdb.log MANIFEST data/*.dat

# 创建测试输入
cat > /tmp/test_input.txt << 'EOF'
PUT name Alice
GET name
PUT age 25
GET age
SNAPSHOT
PUT name Bob
GET name
GET_AT name 1
SCAN a z
STATS
LSM
EXIT
EOF

echo "运行测试命令..."
echo ""
./kvdb < /tmp/test_input.txt

echo ""
echo "=== 测试完成 ==="
