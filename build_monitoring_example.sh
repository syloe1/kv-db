#!/bin/bash

echo "Building KVDB Monitoring Integration Example..."

# 编译监控集成示例
g++ -std=c++17 -O2 \
    monitoring_integration_example.cpp \
    src/monitoring/metrics_collector.cpp \
    src/monitoring/alert_manager.cpp \
    -I. \
    -pthread \
    -o monitoring_integration_example

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo ""
    echo "Running monitoring integration example..."
    echo "========================================"
    
    # 运行示例
    ./monitoring_integration_example
    
    echo ""
    echo "Example completed!"
    echo ""
    echo "Generated files:"
    ls -la kvstore_*.* *.log 2>/dev/null || echo "No output files generated"
    
else
    echo "Build failed!"
    exit 1
fi