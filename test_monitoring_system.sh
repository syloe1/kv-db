#!/bin/bash

echo "Building KVDB Monitoring System Test..."

# 编译监控系统测试
g++ -std=c++17 -O2 \
    test_monitoring_system.cpp \
    src/monitoring/metrics_collector.cpp \
    src/monitoring/alert_manager.cpp \
    src/monitoring/metrics_server.cpp \
    -I. \
    -pthread \
    -o test_monitoring_system

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo ""
    echo "Running monitoring system test..."
    echo "=================================="
    
    # 运行测试
    ./test_monitoring_system
    
    echo ""
    echo "Test completed!"
    echo ""
    echo "Generated files:"
    ls -la test_*.log test_*.prom test_*.json 2>/dev/null || echo "No output files generated"
    
else
    echo "Build failed!"
    exit 1
fi