#include "src/distributed/distributed_kvdb.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

using namespace kvdb::distributed;

void simulate_node(const std::string& node_id, const std::string& host, int port) {
    DistributedKVDB db;
    
    std::cout << "Starting node " << node_id << " on " << host << ":" << port << std::endl;
    
    if (!db.initialize(node_id, host, port)) {
        std::cerr << "Failed to initialize node " << node_id << std::endl;
        return;
    }
    
    // 模拟节点运行
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    db.shutdown();
    std::cout << "Node " << node_id << " shutdown" << std::endl;
}

void demonstrate_distributed_operations() {
    std::cout << "\n=== Distributed KVDB Demonstration ===" << std::endl;
    
    // 创建主节点
    DistributedKVDB master_db;
    if (!master_db.initialize("master", "localhost", 8000)) {
        std::cerr << "Failed to initialize master node" << std::endl;
        return;
    }
    
    // 添加工作节点
    master_db.add_node("worker1", "localhost", 8001);
    master_db.add_node("worker2", "localhost", 8002);
    master_db.add_node("worker3", "localhost", 8003);
    
    std::cout << "Added 3 worker nodes to the cluster" << std::endl;
    
    // 创建分片
    master_db.create_shard("shard_a_m", "a", "m");
    master_db.create_shard("shard_m_z", "m", "z");
    master_db.create_shard("shard_numbers", "0", "9");
    
    std::cout << "Created 3 shards with different key ranges" << std::endl;
    
    // 设置副本因子
    master_db.set_replication_factor(2);
    master_db.set_consistency_level("quorum");
    
    std::cout << "Set replication factor to 2 and consistency level to quorum" << std::endl;
    
    // 插入数据到不同分片
    std::vector<std::pair<std::string, std::string>> test_data = {
        {"apple", "red_fruit"},
        {"banana", "yellow_fruit"},
        {"cherry", "red_fruit"},
        {"mango", "tropical_fruit"},
        {"orange", "citrus_fruit"},
        {"pear", "green_fruit"},
        {"123", "number"},
        {"456", "number"},
        {"789", "number"}
    };
    
    std::cout << "\nInserting test data..." << std::endl;
    for (const auto& [key, value] : test_data) {
        auto response = master_db.put(key, value);
        std::cout << "PUT " << key << " -> " << value 
                  << " : " << (response.success ? "SUCCESS" : "FAILED") << std::endl;
    }
    
    // 读取数据
    std::cout << "\nReading data..." << std::endl;
    for (const auto& [key, expected_value] : test_data) {
        auto response = master_db.get(key);
        std::cout << "GET " << key << " : " 
                  << (response.success ? response.value : "NOT_FOUND") << std::endl;
    }
    
    // 范围扫描
    std::cout << "\nPerforming range scan (a-z)..." << std::endl;
    auto scan_response = master_db.scan("a", "z", 20);
    if (scan_response.success) {
        std::cout << "Scan found " << scan_response.results.size() << " items:" << std::endl;
        for (const auto& [key, value] : scan_response.results) {
            std::cout << "  " << key << " -> " << value << std::endl;
        }
    }
    
    // 分布式事务
    std::cout << "\nTesting distributed transaction..." << std::endl;
    auto tx = master_db.begin_transaction();
    tx->put("grape", "purple_fruit");
    tx->put("kiwi", "green_fruit");
    tx->delete_key("123");
    
    if (tx->commit()) {
        std::cout << "Transaction committed successfully" << std::endl;
    } else {
        std::cout << "Transaction failed to commit" << std::endl;
    }
    
    // 异步操作
    std::cout << "\nTesting async operations..." << std::endl;
    auto async_put = master_db.put_async("watermelon", "large_fruit");
    auto async_get = master_db.get_async("watermelon");
    
    auto put_result = async_put.get();
    auto get_result = async_get.get();
    
    std::cout << "Async PUT result: " << (put_result.success ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "Async GET result: " << (get_result.success ? get_result.value : "FAILED") << std::endl;
    
    // 集群统计
    std::cout << "\nCluster Statistics:" << std::endl;
    auto stats = master_db.get_cluster_stats();
    std::cout << "  Total nodes: " << stats.total_nodes << std::endl;
    std::cout << "  Healthy nodes: " << stats.healthy_nodes << std::endl;
    std::cout << "  Total shards: " << stats.total_shards << std::endl;
    std::cout << "  Failed shards: " << stats.failed_shards << std::endl;
    std::cout << "  Total operations: " << stats.total_operations << std::endl;
    std::cout << "  Average latency: " << stats.average_latency_ms << " ms" << std::endl;
    std::cout << "  Cluster healthy: " << (master_db.is_cluster_healthy() ? "Yes" : "No") << std::endl;
    
    // 模拟负载均衡
    std::cout << "\nTesting load balancing..." << std::endl;
    LoadBalancer lb(LoadBalanceStrategy::ROUND_ROBIN);
    
    NodeInfo node1("worker1", "localhost", 8001);
    NodeInfo node2("worker2", "localhost", 8002);
    NodeInfo node3("worker3", "localhost", 8003);
    
    lb.add_node(node1);
    lb.add_node(node2);
    lb.add_node(node3);
    
    std::cout << "Round-robin selection:" << std::endl;
    for (int i = 0; i < 6; ++i) {
        std::string selected = lb.select_node();
        std::cout << "  Request " << (i+1) << " -> " << selected << std::endl;
    }
    
    // 切换到一致性哈希
    lb.set_strategy(LoadBalanceStrategy::CONSISTENT_HASH);
    std::cout << "Consistent hash selection:" << std::endl;
    std::vector<std::string> test_keys = {"user1", "user2", "user3", "user4", "user5"};
    for (const auto& key : test_keys) {
        std::string selected = lb.select_node_for_key(key);
        std::cout << "  Key '" << key << "' -> " << selected << std::endl;
    }
    
    master_db.shutdown();
    std::cout << "\nDistributed KVDB demonstration completed!" << std::endl;
}

int main() {
    std::cout << "Distributed KVDB Example" << std::endl;
    std::cout << "========================" << std::endl;
    
    try {
        demonstrate_distributed_operations();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}