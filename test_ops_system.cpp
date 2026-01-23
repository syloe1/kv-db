#include "src/ops/ops_manager.h"
#include "src/db/kv_db.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace kvdb;
using namespace kvdb::ops;

void test_data_migration() {
    std::cout << "\n=== 测试数据迁移功能 ===" << std::endl;
    
    // 创建测试数据库
    {
        KvDB source_db("test_source.db");
        for (int i = 0; i < 1000; ++i) {
            source_db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
        }
    }
    
    OpsManager ops_manager;
    ops_manager.initialize();
    ops_manager.start();
    
    // 测试数据导出
    std::cout << "导出数据库到JSON..." << std::endl;
    std::string export_task_id = ops_manager.export_database(
        "test_source.db", "exported_data.json", ExportFormat::JSON);
    
    // 等待导出完成
    auto migration_manager = ops_manager.get_migration_manager();
    while (true) {
        auto task = migration_manager->get_task_status(export_task_id);
        if (task.status == MigrationStatus::COMPLETED) {
            std::cout << "导出完成，处理了 " << task.processed_records << " 条记录" << std::endl;
            break;
        } else if (task.status == MigrationStatus::FAILED) {
            std::cout << "导出失败: " << task.error_message << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 测试数据导入
    std::cout << "从JSON导入数据库..." << std::endl;
    std::string import_task_id = ops_manager.import_database(
        "exported_data.json", "test_target.db", ImportFormat::JSON);
    
    // 等待导入完成
    while (true) {
        auto task = migration_manager->get_task_status(import_task_id);
        if (task.status == MigrationStatus::COMPLETED) {
            std::cout << "导入完成，处理了 " << task.processed_records << " 条记录" << std::endl;
            break;
        } else if (task.status == MigrationStatus::FAILED) {
            std::cout << "导入失败: " << task.error_message << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 验证数据
    {
        KvDB target_db("test_target.db");
        std::string value;
        if (target_db.get("key_100", value)) {
            std::cout << "验证成功: key_100 = " << value << std::endl;
        } else {
            std::cout << "验证失败: 找不到 key_100" << std::endl;
        }
    }
    
    ops_manager.stop();
}

void test_performance_analysis() {
    std::cout << "\n=== 测试性能分析功能 ===" << std::endl;
    
    OpsManager ops_manager;
    ops_manager.initialize();
    ops_manager.start();
    
    auto analyzer = ops_manager.get_performance_analyzer();
    
    // 模拟一些查询
    std::cout << "模拟查询操作..." << std::endl;
    for (int i = 0; i < 100; ++i) {
        auto query_id = analyzer->start_query(QueryType::GET, "key_" + std::to_string(i));
        
        // 模拟查询延迟
        std::this_thread::sleep_for(std::chrono::microseconds(100 + (i % 10) * 100));
        
        analyzer->end_query(query_id, true, 64, "test_client");
    }
    
    // 模拟一些慢查询
    std::cout << "模拟慢查询..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        auto query_id = analyzer->start_query(QueryType::SCAN, "slow_key_" + std::to_string(i));
        
        // 模拟慢查询延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(10 + i * 5));
        
        analyzer->end_query(query_id, true, 1024, "slow_client");
    }
    
    // 获取性能统计
    auto stats = analyzer->get_performance_stats();
    std::cout << "性能统计:" << std::endl;
    std::cout << "  总查询数: " << stats.total_queries << std::endl;
    std::cout << "  慢查询数: " << stats.slow_queries << std::endl;
    std::cout << "  平均延迟: " << stats.avg_latency.count() << " 微秒" << std::endl;
    std::cout << "  P95延迟: " << stats.p95_latency.count() << " 微秒" << std::endl;
    std::cout << "  最大延迟: " << stats.max_latency.count() << " 微秒" << std::endl;
    
    // 获取慢查询列表
    auto slow_queries = analyzer->get_slow_queries(10);
    std::cout << "慢查询列表 (" << slow_queries.size() << " 条):" << std::endl;
    for (const auto& query : slow_queries) {
        std::cout << "  查询ID: " << query.query_id 
                  << ", 延迟: " << query.duration.count() << " 微秒"
                  << ", 键: " << query.key_pattern << std::endl;
    }
    
    // 生成性能报告
    std::string report = analyzer->generate_performance_report();
    std::cout << "\n性能报告:\n" << report << std::endl;
    
    ops_manager.stop();
}

void test_capacity_planning() {
    std::cout << "\n=== 测试容量规划功能 ===" << std::endl;
    
    OpsManager ops_manager;
    ops_manager.initialize();
    ops_manager.start();
    
    auto planner = ops_manager.get_capacity_planner();
    
    // 模拟容量指标数据
    std::cout << "记录容量指标..." << std::endl;
    for (int i = 0; i < 50; ++i) {
        CapacityMetrics metrics;
        metrics.total_storage_bytes = 1000000000; // 1GB
        metrics.used_storage_bytes = 500000000 + i * 10000000; // 逐渐增长
        metrics.available_storage_bytes = metrics.total_storage_bytes - metrics.used_storage_bytes;
        metrics.storage_utilization = static_cast<double>(metrics.used_storage_bytes) / metrics.total_storage_bytes;
        
        metrics.total_memory_bytes = 2000000000; // 2GB
        metrics.used_memory_bytes = 800000000 + i * 5000000; // 逐渐增长
        metrics.available_memory_bytes = metrics.total_memory_bytes - metrics.used_memory_bytes;
        metrics.memory_utilization = static_cast<double>(metrics.used_memory_bytes) / metrics.total_memory_bytes;
        
        metrics.current_qps = 1000 + i * 10; // QPS逐渐增长
        metrics.max_qps = 5000;
        metrics.avg_latency_ms = 1.0 + (i % 10) * 0.1;
        
        metrics.active_connections = 50 + i;
        metrics.max_connections = 1000;
        
        metrics.timestamp = std::chrono::system_clock::now() - std::chrono::minutes(50 - i);
        
        planner->record_metrics(metrics);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 获取当前指标
    auto current_metrics = planner->get_current_metrics();
    std::cout << "当前容量指标:" << std::endl;
    std::cout << "  存储使用率: " << (current_metrics.storage_utilization * 100) << "%" << std::endl;
    std::cout << "  内存使用率: " << (current_metrics.memory_utilization * 100) << "%" << std::endl;
    std::cout << "  当前QPS: " << current_metrics.current_qps << std::endl;
    std::cout << "  活跃连接: " << current_metrics.active_connections << std::endl;
    
    // 获取增长预测
    std::cout << "\n存储增长预测:" << std::endl;
    auto storage_prediction = planner->predict_storage_growth();
    std::cout << "  增长模式: " << static_cast<int>(storage_prediction.pattern) << std::endl;
    std::cout << "  置信度: " << (storage_prediction.confidence_score * 100) << "%" << std::endl;
    std::cout << "  增长率: " << (storage_prediction.growth_rate * 100) << "%/天" << std::endl;
    
    // 获取容量预警
    auto alerts = planner->get_capacity_alerts();
    std::cout << "\n容量预警 (" << alerts.size() << " 条):" << std::endl;
    for (const auto& alert : alerts) {
        std::cout << "  类型: " << static_cast<int>(alert.type) 
                  << ", 消息: " << alert.message << std::endl;
        std::cout << "  当前值: " << alert.current_value 
                  << ", 阈值: " << alert.threshold_value << std::endl;
    }
    
    // 获取扩容建议
    auto recommendations = planner->get_scaling_recommendations();
    std::cout << "\n扩容建议 (" << recommendations.size() << " 条):" << std::endl;
    for (const auto& rec : recommendations) {
        std::cout << "  类型: " << static_cast<int>(rec.type) << std::endl;
        std::cout << "  描述: " << rec.description << std::endl;
        std::cout << "  建议内存: " << rec.recommended_memory_gb << " GB" << std::endl;
        std::cout << "  建议存储: " << rec.recommended_storage_gb << " GB" << std::endl;
        std::cout << "  优先级: " << rec.priority << std::endl;
    }
    
    ops_manager.stop();
}

void test_automation() {
    std::cout << "\n=== 测试自动化运维功能 ===" << std::endl;
    
    OpsManager ops_manager;
    ops_manager.initialize();
    ops_manager.start();
    
    // 创建自动扩容策略
    ScalingPolicy scaling_policy;
    scaling_policy.name = "测试扩容策略";
    scaling_policy.scale_up_cpu_threshold = 0.8;
    scaling_policy.scale_up_memory_threshold = 0.8;
    scaling_policy.min_instances = 1;
    scaling_policy.max_instances = 5;
    
    std::string scaling_policy_id = ops_manager.create_auto_scaling_policy(scaling_policy);
    std::cout << "创建扩容策略: " << scaling_policy_id << std::endl;
    
    // 创建自动备份策略
    BackupPolicy backup_policy;
    backup_policy.name = "测试备份策略";
    backup_policy.type = BackupPolicy::BackupType::FULL;
    backup_policy.schedule = "0 2 * * *"; // 每天凌晨2点
    backup_policy.backup_path = "./backups";
    backup_policy.retention_days = 7;
    
    std::string backup_policy_id = ops_manager.create_auto_backup_policy(backup_policy);
    std::cout << "创建备份策略: " << backup_policy_id << std::endl;
    
    // 创建自动清理策略
    CleanupPolicy cleanup_policy;
    cleanup_policy.name = "测试清理策略";
    cleanup_policy.target_paths = {"./logs", "./temp"};
    cleanup_policy.file_patterns = {"*.log", "*.tmp"};
    cleanup_policy.max_age = std::chrono::hours(24 * 7); // 7天
    cleanup_policy.schedule = "0 3 * * 0"; // 每周日凌晨3点
    
    std::string cleanup_policy_id = ops_manager.create_auto_cleanup_policy(cleanup_policy);
    std::cout << "创建清理策略: " << cleanup_policy_id << std::endl;
    
    // 获取自动化状态
    auto automation_manager = ops_manager.get_automation_manager();
    auto stats = automation_manager->get_automation_stats();
    std::cout << "\n自动化统计:" << std::endl;
    std::cout << "  总任务数: " << stats.total_tasks << std::endl;
    std::cout << "  活跃任务数: " << stats.active_tasks << std::endl;
    std::cout << "  成功执行数: " << stats.successful_executions << std::endl;
    std::cout << "  失败执行数: " << stats.failed_executions << std::endl;
    
    ops_manager.stop();
}

void test_dashboard() {
    std::cout << "\n=== 测试运维仪表板 ===" << std::endl;
    
    OpsManager ops_manager;
    ops_manager.initialize();
    ops_manager.start();
    
    // 获取仪表板数据
    auto dashboard = ops_manager.get_dashboard_data();
    
    std::cout << "系统状态:" << std::endl;
    std::cout << "  健康状态: " << (dashboard.system_status.healthy ? "健康" : "异常") << std::endl;
    std::cout << "  状态消息: " << dashboard.system_status.status_message << std::endl;
    
    std::cout << "\n性能指标:" << std::endl;
    std::cout << "  总查询数: " << dashboard.performance_stats.total_queries << std::endl;
    std::cout << "  慢查询数: " << dashboard.performance_stats.slow_queries << std::endl;
    std::cout << "  QPS: " << dashboard.performance_stats.qps << std::endl;
    
    std::cout << "\n容量指标:" << std::endl;
    std::cout << "  存储使用率: " << (dashboard.capacity_metrics.storage_utilization * 100) << "%" << std::endl;
    std::cout << "  内存使用率: " << (dashboard.capacity_metrics.memory_utilization * 100) << "%" << std::endl;
    
    std::cout << "\n自动化状态:" << std::endl;
    std::cout << "  活跃任务: " << dashboard.automation_status.active_tasks << std::endl;
    std::cout << "  完成任务: " << dashboard.automation_status.completed_tasks << std::endl;
    std::cout << "  失败任务: " << dashboard.automation_status.failed_tasks << std::endl;
    
    std::cout << "\n活跃告警 (" << dashboard.active_alerts.size() << " 条):" << std::endl;
    for (const auto& alert : dashboard.active_alerts) {
        std::cout << "  " << alert.message << std::endl;
    }
    
    // 生成综合报告
    std::string ops_report = ops_manager.generate_ops_report();
    std::cout << "\n=== 运维综合报告 ===" << std::endl;
    std::cout << ops_report << std::endl;
    
    ops_manager.stop();
}

int main() {
    std::cout << "KVDB 运维工具系统测试" << std::endl;
    std::cout << "========================" << std::endl;
    
    try {
        // 测试各个功能模块
        test_data_migration();
        test_performance_analysis();
        test_capacity_planning();
        test_automation();
        test_dashboard();
        
        std::cout << "\n所有测试完成！" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}