#include "src/monitoring/metrics_collector.h"
#include "src/monitoring/alert_manager.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <fstream>

using namespace kvdb::monitoring;

void simulate_workload() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> latency_dist(10.0, 200.0);
    std::uniform_int_distribution<> op_dist(0, 3);
    
    const std::vector<std::string> operations = {"get", "put", "delete", "scan"};
    
    for (int i = 0; i < 100; ++i) {
        // 模拟请求
        RECORD_REQUEST();
        
        // 模拟延迟
        double latency = latency_dist(gen);
        RECORD_LATENCY(latency);
        
        // 模拟操作
        std::string op = operations[op_dist(gen)];
        RECORD_OPERATION(op);
        
        // 模拟数据变化
        UPDATE_KEY_COUNT(10000 + i);
        
        if (g_metrics_collector) {
            g_metrics_collector->update_data_size(1024 * 1024 * (100 + i));
            g_metrics_collector->record_throughput(1024);
            
            // 模拟连接变化
            if (i % 10 == 0) {
                g_metrics_collector->update_connections(1);
            }
            
            // 模拟事务
            if (i % 5 == 0) {
                g_metrics_collector->record_transaction(i % 7 != 0); // 大部分提交
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void test_metrics_collection() {
    std::cout << "=== Testing Metrics Collection ===" << std::endl;
    
    // 创建指标收集器
    g_metrics_collector = std::make_unique<MetricsCollector>();
    g_metrics_collector->start();
    
    std::cout << "Starting workload simulation..." << std::endl;
    
    // 启动工作负载模拟
    std::thread workload_thread(simulate_workload);
    
    // 等待一段时间收集指标
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 获取指标
    auto perf_metrics = g_metrics_collector->get_performance_metrics();
    auto resource_metrics = g_metrics_collector->get_resource_metrics();
    auto business_metrics = g_metrics_collector->get_business_metrics();
    
    std::cout << "\n--- Performance Metrics ---" << std::endl;
    std::cout << "QPS: " << perf_metrics.qps << std::endl;
    std::cout << "Total Requests: " << perf_metrics.total_requests << std::endl;
    std::cout << "Average Latency: " << perf_metrics.avg_latency << " ms" << std::endl;
    std::cout << "P99 Latency: " << perf_metrics.p99_latency << " ms" << std::endl;
    std::cout << "GET Count: " << perf_metrics.get_count << std::endl;
    std::cout << "PUT Count: " << perf_metrics.put_count << std::endl;
    
    std::cout << "\n--- Resource Metrics ---" << std::endl;
    std::cout << "CPU Usage: " << resource_metrics.cpu_usage << "%" << std::endl;
    std::cout << "Memory Usage: " << (resource_metrics.memory_usage / 1024 / 1024) << " MB" << std::endl;
    
    std::cout << "\n--- Business Metrics ---" << std::endl;
    std::cout << "Total Keys: " << business_metrics.total_keys << std::endl;
    std::cout << "Total Data Size: " << (business_metrics.total_data_size / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Active Connections: " << business_metrics.active_connections << std::endl;
    std::cout << "Transaction Count: " << business_metrics.transaction_count << std::endl;
    std::cout << "Commit Count: " << business_metrics.commit_count << std::endl;
    
    workload_thread.join();
    g_metrics_collector->stop();
}

void test_alert_system() {
    std::cout << "\n=== Testing Alert System ===" << std::endl;
    
    // 创建告警管理器
    g_alert_manager = std::make_unique<AlertManager>();
    
    // 添加告警处理器
    g_alert_manager->add_handler(std::make_unique<LogAlertHandler>("test_alerts.log"));
    
    // 添加默认规则
    g_alert_manager->add_default_rules();
    
    // 添加自定义规则
    AlertRule custom_rule;
    custom_rule.name = "test_rule";
    custom_rule.metric_name = "qps";
    custom_rule.condition = [](double value) { return value > 10.0; };
    custom_rule.level = AlertLevel::WARNING;
    custom_rule.message_template = "QPS is high: {value}";
    custom_rule.cooldown = std::chrono::seconds(5);
    g_alert_manager->add_rule(custom_rule);
    
    g_alert_manager->start();
    
    // 重新启动指标收集器以触发告警
    g_metrics_collector = std::make_unique<MetricsCollector>();
    g_metrics_collector->start();
    
    // 模拟高负载以触发告警
    std::cout << "Simulating high load to trigger alerts..." << std::endl;
    
    for (int i = 0; i < 50; ++i) {
        for (int j = 0; j < 5; ++j) {
            RECORD_REQUEST();
            RECORD_LATENCY(150.0); // 高延迟
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 等待告警处理
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 检查告警历史
    auto alerts = g_alert_manager->get_alert_history();
    std::cout << "Generated " << alerts.size() << " alerts:" << std::endl;
    
    for (const auto& alert : alerts) {
        std::string level_str;
        switch (alert.level) {
            case AlertLevel::INFO: level_str = "INFO"; break;
            case AlertLevel::WARNING: level_str = "WARNING"; break;
            case AlertLevel::ERROR: level_str = "ERROR"; break;
            case AlertLevel::CRITICAL: level_str = "CRITICAL"; break;
        }
        
        std::cout << "[" << level_str << "] " << alert.metric_name 
                  << ": " << alert.message << " (value: " << alert.current_value << ")" << std::endl;
    }
    
    g_alert_manager->stop();
    g_metrics_collector->stop();
}

void test_prometheus_export() {
    std::cout << "\n=== Testing Prometheus Export ===" << std::endl;
    
    g_metrics_collector = std::make_unique<MetricsCollector>();
    g_metrics_collector->start();
    
    // 生成一些指标数据
    for (int i = 0; i < 20; ++i) {
        RECORD_REQUEST();
        RECORD_LATENCY(25.0 + i);
        RECORD_OPERATION("get");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 导出Prometheus格式
    std::string prometheus_metrics = g_metrics_collector->export_prometheus();
    std::cout << "Prometheus format metrics:" << std::endl;
    std::cout << prometheus_metrics << std::endl;
    
    // 导出JSON格式
    std::string json_metrics = g_metrics_collector->export_json();
    std::cout << "JSON format metrics:" << std::endl;
    std::cout << json_metrics << std::endl;
    
    // 保存到文件
    std::ofstream prom_file("test_metrics.prom");
    if (prom_file.is_open()) {
        prom_file << prometheus_metrics;
        prom_file.close();
        std::cout << "Prometheus metrics saved to test_metrics.prom" << std::endl;
    }
    
    std::ofstream json_file("test_metrics.json");
    if (json_file.is_open()) {
        json_file << json_metrics;
        json_file.close();
        std::cout << "JSON metrics saved to test_metrics.json" << std::endl;
    }
    
    g_metrics_collector->stop();
}

int main() {
    std::cout << "KVDB Monitoring System Test (Simplified)" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    try {
        // 测试指标收集
        test_metrics_collection();
        
        // 测试告警系统
        test_alert_system();
        
        // 测试Prometheus导出
        test_prometheus_export();
        
        std::cout << "\n=== All Tests Completed Successfully ===" << std::endl;
        
        // 检查生成的文件
        std::cout << "\nGenerated files:" << std::endl;
        std::ifstream alert_file("test_alerts.log");
        if (alert_file.is_open()) {
            std::cout << "- test_alerts.log (alert log)" << std::endl;
            alert_file.close();
        }
        
        std::ifstream prom_file("test_metrics.prom");
        if (prom_file.is_open()) {
            std::cout << "- test_metrics.prom (Prometheus metrics)" << std::endl;
            prom_file.close();
        }
        
        std::ifstream json_file("test_metrics.json");
        if (json_file.is_open()) {
            std::cout << "- test_metrics.json (JSON metrics)" << std::endl;
            json_file.close();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}