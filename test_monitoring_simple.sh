#!/bin/bash

echo "Building KVDB Monitoring System Test (Simplified)..."

# 编译简化版监控系统测试
g++ -std=c++17 -O2 \
    test_monitoring_simple.cpp \
    src/monitoring/metrics_collector.cpp \
    src/monitoring/alert_manager.cpp \
    -I. \
    -pthread \
    -o test_monitoring_simple

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo ""
    echo "Running monitoring system test..."
    echo "=================================="
    
    # 运行测试
    ./test_monitoring_simple
    
    echo ""
    echo "Test completed!"
    echo ""
    echo "Generated files:"
    ls -la test_*.log test_*.prom test_*.json 2>/dev/null || echo "No output files generated"
    
else
    echo "Build failed!"
    exit 1
fi