#include "src/monitoring/metrics_collector.h"
#include "src/monitoring/alert_manager.h"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>
#include <memory>
#include <cstdlib>
#include <ctime>

using namespace kvdb::monitoring;

// 模拟的KV存储类
class SimpleKVStore {
private:
    std::map<std::string, std::string> data_;
    std::mutex mutex_;
    
public:
    SimpleKVStore() {
        // 初始化监控系统
        g_metrics_collector = std::make_unique<MetricsCollector>();
        g_alert_manager = std::make_unique<AlertManager>();
        
        // 添加告警处理器
        g_alert_manager->add_handler(std::make_unique<LogAlertHandler>("kvstore_alerts.log"));
        
        // 添加自定义告警规则
        AlertRule latency_rule;
        latency_rule.name = "high_latency_warning";
        latency_rule.metric_name = "avg_latency";
        latency_rule.condition = [](double value) { return value > 50.0; };
        latency_rule.level = AlertLevel::WARNING;
        latency_rule.message_template = "KV Store latency is high: {value}ms";
        latency_rule.cooldown = std::chrono::seconds(30);
        g_alert_manager->add_rule(latency_rule);
        
        // 启动监控
        g_metrics_collector->start();
        g_alert_manager->start();
        
        std::cout << "KV Store initialized with monitoring enabled" << std::endl;
    }
    
    ~SimpleKVStore() {
        if (g_alert_manager) g_alert_manager->stop();
        if (g_metrics_collector) g_metrics_collector->stop();
    }
    
    std::string get(const std::string& key) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // 记录请求
        RECORD_REQUEST();
        
        // 执行实际操作
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        std::string result = (it != data_.end()) ? it->second : "";
        
        // 模拟一些延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(10 + rand() % 40));
        
        // 记录延迟和操作
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration<double, std::milli>(end - start).count();
        RECORD_LATENCY(latency);
        RECORD_OPERATION("get");
        
        return result;
    }
    
    void put(const std::string& key, const std::string& value) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // 记录请求
        RECORD_REQUEST();
        
        // 执行实际操作
        std::lock_guard<std::mutex> lock(mutex_);
        data_[key] = value;
        
        // 模拟一些延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(15 + rand() % 30));
        
        // 更新业务指标
        UPDATE_KEY_COUNT(data_.size());
        
        if (g_metrics_collector) {
            // 估算数据大小
            size_t total_size = 0;
            for (const auto& pair : data_) {
                total_size += pair.first.size() + pair.second.size();
            }
            g_metrics_collector->update_data_size(total_size);
        }
        
        // 记录延迟和操作
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration<double, std::milli>(end - start).count();
        RECORD_LATENCY(latency);
        RECORD_OPERATION("put");
    }
    
    void delete_key(const std::string& key) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // 记录请求
        RECORD_REQUEST();
        
        // 执行实际操作
        std::lock_guard<std::mutex> lock(mutex_);
        data_.erase(key);
        
        // 模拟一些延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(8 + rand() % 20));
        
        // 更新业务指标
        UPDATE_KEY_COUNT(data_.size());
        
        // 记录延迟和操作
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration<double, std::milli>(end - start).count();
        RECORD_LATENCY(latency);
        RECORD_OPERATION("delete");
    }
    
    void print_metrics() {
        if (!g_metrics_collector) return;
        
        auto perf = g_metrics_collector->get_performance_metrics();
        auto business = g_metrics_collector->get_business_metrics();
        
        std::cout << "\n=== KV Store Metrics ===" << std::endl;
        std::cout << "QPS: " << perf.qps << std::endl;
        std::cout << "Total Requests: " << perf.total_requests << std::endl;
        std::cout << "Average Latency: " << perf.avg_latency << " ms" << std::endl;
        std::cout << "GET Operations: " << perf.get_count << std::endl;
        std::cout << "PUT Operations: " << perf.put_count << std::endl;
        std::cout << "DELETE Operations: " << perf.delete_count << std::endl;
        std::cout << "Total Keys: " << business.total_keys << std::endl;
        std::cout << "Data Size: " << business.total_data_size << " bytes" << std::endl;
    }
};

// 模拟客户端连接
class KVClient {
private:
    SimpleKVStore* store_;
    int client_id_;
    
public:
    KVClient(SimpleKVStore* store, int id) : store_(store), client_id_(id) {
        // 记录新连接
        if (g_metrics_collector) {
            g_metrics_collector->update_connections(1);
        }
    }
    
    ~KVClient() {
        // 记录连接断开
        if (g_metrics_collector) {
            g_metrics_collector->update_connections(-1);
        }
    }
    
    void simulate_workload(int operations = 50) {
        std::cout << "Client " << client_id_ << " starting workload..." << std::endl;
        
        for (int i = 0; i < operations; ++i) {
            int op_type = rand() % 3;
            std::string key = "key_" + std::to_string(client_id_) + "_" + std::to_string(i);
            
            switch (op_type) {
                case 0: // GET
                    store_->get(key);
                    break;
                case 1: // PUT
                    store_->put(key, "value_" + std::to_string(i));
                    break;
                case 2: // DELETE
                    store_->delete_key(key);
                    break;
            }
            
            // 随机间隔
            std::this_thread::sleep_for(std::chrono::milliseconds(10 + rand() % 50));
        }
        
        std::cout << "Client " << client_id_ << " completed workload" << std::endl;
    }
};

int main() {
    std::cout << "KVDB Monitoring Integration Example" << std::endl;
    std::cout << "====================================" << std::endl;
    
    srand(time(nullptr));
    
    // 创建KV存储
    SimpleKVStore store;
    
    // 等待监控系统启动
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 创建多个客户端模拟并发访问
    std::vector<std::thread> client_threads;
    std::vector<std::unique_ptr<KVClient>> clients;
    
    for (int i = 0; i < 3; ++i) {
        clients.push_back(std::make_unique<KVClient>(&store, i));
        client_threads.emplace_back([&clients, i]() {
            clients[i]->simulate_workload(30);
        });
    }
    
    // 定期打印指标
    std::thread metrics_thread([&store]() {
        for (int i = 0; i < 6; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            store.print_metrics();
        }
    });
    
    // 等待所有客户端完成
    for (auto& thread : client_threads) {
        thread.join();
    }
    
    // 等待指标线程完成
    metrics_thread.join();
    
    // 最终指标报告
    std::cout << "\n=== Final Metrics Report ===" << std::endl;
    store.print_metrics();
    
    // 导出指标到文件
    if (g_metrics_collector) {
        std::ofstream prom_file("kvstore_metrics.prom");
        if (prom_file.is_open()) {
            prom_file << g_metrics_collector->export_prometheus();
            prom_file.close();
            std::cout << "\nMetrics exported to kvstore_metrics.prom" << std::endl;
        }
        
        std::ofstream json_file("kvstore_metrics.json");
        if (json_file.is_open()) {
            json_file << g_metrics_collector->export_json();
            json_file.close();
            std::cout << "Metrics exported to kvstore_metrics.json" << std::endl;
        }
    }
    
    // 检查告警
    if (g_alert_manager) {
        auto alerts = g_alert_manager->get_alert_history();
        if (!alerts.empty()) {
            std::cout << "\nGenerated " << alerts.size() << " alerts:" << std::endl;
            for (const auto& alert : alerts) {
                std::string level_str;
                switch (alert.level) {
                    case AlertLevel::INFO: level_str = "INFO"; break;
                    case AlertLevel::WARNING: level_str = "WARNING"; break;
                    case AlertLevel::ERROR: level_str = "ERROR"; break;
                    case AlertLevel::CRITICAL: level_str = "CRITICAL"; break;
                }
                std::cout << "[" << level_str << "] " << alert.message << std::endl;
            }
        } else {
            std::cout << "\nNo alerts generated during this session" << std::endl;
        }
    }
    
    std::cout << "\nMonitoring integration example completed successfully!" << std::endl;
    
    return 0;
}