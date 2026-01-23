#!/bin/bash

echo "编译运维工具系统测试..."

# 创建构建目录
mkdir -p build

# 编译测试程序
g++ -std=c++17 -O2 \
    -I. \
    test_ops_system.cpp \
    src/db/kv_db.cpp \
    src/storage/memtable.cpp \
    src/storage/concurrent_memtable.cpp \
    src/sstable/sstable_writer.cpp \
    src/sstable/sstable_reader.cpp \
    src/sstable/block_index.cpp \
    src/log/wal.cpp \
    src/iterator/memtable_iterator.cpp \
    src/iterator/sstable_iterator.cpp \
    src/iterator/merge_iterator.cpp \
    src/compaction/compactor.cpp \
    src/version/version_set.cpp \
    src/cache/cache_manager.cpp \
    src/bloom/bloom_filter.cpp \
    -lpthread \
    -o build/test_ops_system

if [ $? -eq 0 ]; then
    echo "编译成功！"
    echo "运行运维工具系统测试..."
    echo "========================"
    
    # 创建测试目录
    mkdir -p test_data
    mkdir -p backups
    mkdir -p logs
    mkdir -p temp
    mkdir -p ops_reports
    
    # 运行测试
    cd build
    ./test_ops_system
    
    echo ""
    echo "测试完成！"
    echo ""
    echo "生成的文件："
    ls -la ../exported_data.json 2>/dev/null || echo "  exported_data.json (未生成)"
    ls -la ../test_source.db* 2>/dev/null || echo "  test_source.db (未生成)"
    ls -la ../test_target.db* 2>/dev/null || echo "  test_target.db (未生成)"
    
    echo ""
    echo "运维工具功能说明："
    echo "=================="
    echo "1. 数据迁移管理器 (DataMigrationManager)"
    echo "   - 支持JSON、CSV、Binary等格式的数据导入导出"
    echo "   - 支持数据库间的直接迁移"
    echo "   - 支持增量同步和数据验证"
    echo "   - 提供任务进度跟踪和错误处理"
    echo ""
    echo "2. 性能分析器 (PerformanceAnalyzer)"
    echo "   - 慢查询检测和分析"
    echo "   - 性能统计和延迟分析"
    echo "   - 查询模式识别和热点键分析"
    echo "   - 自动生成性能报告"
    echo ""
    echo "3. 容量规划器 (CapacityPlanner)"
    echo "   - 数据增长预测和趋势分析"
    echo "   - 容量预警和阈值监控"
    echo "   - 扩容建议和成本估算"
    echo "   - 支持多种增长模式识别"
    echo ""
    echo "4. 自动化管理器 (AutomationManager)"
    echo "   - 自动扩容策略和执行"
    echo "   - 自动备份调度和管理"
    echo "   - 自动清理和维护任务"
    echo "   - 基于时间、指标、事件的触发机制"
    echo ""
    echo "5. 运维管理器 (OpsManager)"
    echo "   - 统一的运维接口和仪表板"
    echo "   - 综合健康检查和状态监控"
    echo "   - 自动化报告生成和数据导出"
    echo "   - 告警通知和事件处理"
    
else
    echo "编译失败！"
    exit 1
fi