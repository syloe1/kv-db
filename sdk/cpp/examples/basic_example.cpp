#include "kvdb_client.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace kvdb::client;

int main() {
    // 配置客户端
    ClientConfig config;
    config.server_address = "localhost:50051";
    config.protocol = "grpc";
    config.connection_timeout_ms = 5000;
    config.request_timeout_ms = 10000;
    
    // 创建客户端
    auto client = ClientFactory::create_client(config);
    
    // 连接到服务器
    auto connect_result = client->connect();
    if (!connect_result.success) {
        std::cerr << "Failed to connect: " << connect_result.error_message << std::endl;
        return 1;
    }
    
    std::cout << "Connected to KVDB server" << std::endl;
    
    // 基本操作示例
    std::cout << "\n=== Basic Operations ===" << std::endl;
    
    // PUT操作
    auto put_result = client->put("user:1001", "Alice");
    if (put_result.success) {
        std::cout << "PUT user:1001 = Alice" << std::endl;
    } else {
        std::cerr << "PUT failed: " << put_result.error_message << std::endl;
    }
    
    // GET操作
    auto get_result = client->get("user:1001");
    if (get_result.success) {
        std::cout << "GET user:1001 = " << get_result.value << std::endl;
    } else {
        std::cerr << "GET failed: " << get_result.error_message << std::endl;
    }
    
    // 批量操作示例
    std::cout << "\n=== Batch Operations ===" << std::endl;
    
    std::vector<KeyValue> batch_data = {
        {"user:1002", "Bob"},
        {"user:1003", "Charlie"},
        {"user:1004", "David"}
    };
    
    auto batch_put_result = client->batch_put(batch_data);
    if (batch_put_result.success) {
        std::cout << "Batch PUT completed" << std::endl;
    }
    
    std::vector<std::string> keys = {"user:1001", "user:1002", "user:1003"};
    auto batch_get_result = client->batch_get(keys);
    if (batch_get_result.success) {
        std::cout << "Batch GET results:" << std::endl;
        for (const auto& kv : batch_get_result.value) {
            std::cout << "  " << kv.key << " = " << kv.value << std::endl;
        }
    }
    
    // 扫描操作示例
    std::cout << "\n=== Scan Operations ===" << std::endl;
    
    auto prefix_scan_result = client->prefix_scan("user:", 10);
    if (prefix_scan_result.success) {
        std::cout << "Prefix scan results:" << std::endl;
        for (const auto& kv : prefix_scan_result.value) {
            std::cout << "  " << kv.key << " = " << kv.value << std::endl;
        }
    }
    
    // 快照操作示例
    std::cout << "\n=== Snapshot Operations ===" << std::endl;
    
    auto snapshot_result = client->create_snapshot();
    if (snapshot_result.success) {
        auto snapshot = snapshot_result.value;
        std::cout << "Created snapshot: " << snapshot->id() << std::endl;
        
        // 在快照上读取数据
        auto snapshot_get_result = client->get_at_snapshot("user:1001", snapshot);
        if (snapshot_get_result.success) {
            std::cout << "Snapshot GET user:1001 = " << snapshot_get_result.value << std::endl;
        }
        
        // 释放快照
        client->release_snapshot(snapshot);
        std::cout << "Released snapshot" << std::endl;
    }
    
    // 异步操作示例
    std::cout << "\n=== Async Operations ===" << std::endl;
    
    auto async_put = client->put_async("async:key", "async_value");
    auto async_get = client->get_async("user:1001");
    
    // 等待异步操作完成
    auto async_put_result = async_put.get();
    auto async_get_result = async_get.get();
    
    if (async_put_result.success) {
        std::cout << "Async PUT completed" << std::endl;
    }
    
    if (async_get_result.success) {
        std::cout << "Async GET result: " << async_get_result.value << std::endl;
    }
    
    // 订阅示例
    std::cout << "\n=== Subscription Example ===" << std::endl;
    
    auto subscription_result = client->subscribe("user:*", 
        [](const std::string& key, const std::string& value, const std::string& operation) {
            std::cout << "Subscription event: " << operation << " " << key << " = " << value << std::endl;
        }, true);
    
    if (subscription_result.success) {
        auto subscription = subscription_result.value;
        std::cout << "Started subscription" << std::endl;
        
        // 等待一段时间接收事件
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 触发一些事件
        client->put("user:1005", "Eve");
        client->del("user:1004");
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 取消订阅
        subscription->cancel();
        std::cout << "Cancelled subscription" << std::endl;
    }
    
    // 管理操作示例
    std::cout << "\n=== Management Operations ===" << std::endl;
    
    auto stats_result = client->get_stats();
    if (stats_result.success) {
        std::cout << "Database stats:" << std::endl;
        for (const auto& stat : stats_result.value) {
            std::cout << "  " << stat.first << " = " << stat.second << std::endl;
        }
    }
    
    // 断开连接
    client->disconnect();
    std::cout << "\nDisconnected from server" << std::endl;
    
    return 0;
}