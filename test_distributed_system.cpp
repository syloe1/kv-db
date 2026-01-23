#include "src/distributed/distributed_kvdb.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cassert>

using namespace kvdb::distributed;

void test_shard_management() {
    std::cout << "\n=== Testing Shard Management ===" << std::endl;
    
    DistributedKVDB db;
    assert(db.initialize("node1", "localhost", 8001));
    
    // 添加更多节点
    assert(db.add_node("node2", "localhost", 8002));
    assert(db.add_node("node3", "localhost", 8003));
    
    // 创建分片
    assert(db.create_shard("shard1", "a", "m"));
    assert(db.create_shard("shard2", "m", "z"));
    
    // 测试键分片
    auto response1 = db.put("apple", "fruit");
    assert(response1.success);
    
    auto response2 = db.put("zebra", "animal");
    assert(response2.success);
    
    // 读取数据
    auto get_response1 = db.get("apple");
    std::cout << "Get apple: " << (get_response1.success ? "success" : "failed") << std::endl;
    
    auto get_response2 = db.get("zebra");
    std::cout << "Get zebra: " << (get_response2.success ? "success" : "failed") << std::endl;
    
    db.shutdown();
    std::cout << "Shard management test passed!" << std::endl;
}

void test_load_balancing() {
    std::cout << "\n=== Testing Load Balancing ===" << std::endl;
    
    LoadBalancer lb(LoadBalanceStrategy::ROUND_ROBIN);
    
    // 添加节点
    NodeInfo node1("node1", "localhost", 8001);
    NodeInfo node2("node2", "localhost", 8002);
    NodeInfo node3("node3", "localhost", 8003);
    
    assert(lb.add_node(node1));
    assert(lb.add_node(node2));
    assert(lb.add_node(node3));
    
    // 测试轮询选择
    std::vector<std::string> selected_nodes;
    for (int i = 0; i < 6; ++i) {
        std::string node = lb.select_node();
        selected_nodes.push_back(node);
        std::cout << "Selected node: " << node << std::endl;
    }
    
    // 测试一致性哈希
    lb.set_strategy(LoadBalanceStrategy::CONSISTENT_HASH);
    std::string node_for_key1 = lb.select_node_for_key("test_key_1");
    std::string node_for_key2 = lb.select_node_for_key("test_key_1");  // 同一个key
    assert(node_for_key1 == node_for_key2);  // 应该选择同一个节点
    
    std::cout << "Key 'test_key_1' maps to node: " << node_for_key1 << std::endl;
    
    // 测试副本选择
    auto replicas = lb.select_replica_nodes(2);
    std::cout << "Selected " << replicas.size() << " replica nodes" << std::endl;
    
    std::cout << "Load balancing test passed!" << std::endl;
}

void test_failover() {
    std::cout << "\n=== Testing Failover ===" << std::endl;
    
    DistributedKVDB db;
    assert(db.initialize("node1", "localhost", 8001));
    
    // 添加节点
    assert(db.add_node("node2", "localhost", 8002));
    assert(db.add_node("node3", "localhost", 8003));
    
    // 创建分片
    assert(db.create_shard("shard1", "", "~"));
    
    // 模拟节点故障
    LoadBalancer lb;
    NodeInfo node1("node1", "localhost", 8001);
    NodeInfo node2("node2", "localhost", 8002);
    lb.add_node(node1);
    lb.add_node(node2);
    
    ShardManager sm;
    FailoverManager fm(&sm, &lb);
    
    assert(fm.start_monitoring());
    
    // 模拟节点故障
    lb.mark_node_unhealthy("node1");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 检查故障转移状态
    auto state = fm.get_failover_state();
    std::cout << "Failover state: " << static_cast<int>(state) << std::endl;
    
    // 恢复节点
    lb.mark_node_healthy("node1");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    assert(fm.stop_monitoring());
    
    db.shutdown();
    std::cout << "Failover test passed!" << std::endl;
}

void test_distributed_transactions() {
    std::cout << "\n=== Testing Distributed Transactions ===" << std::endl;
    
    DistributedKVDB db;
    assert(db.initialize("node1", "localhost", 8001));
    
    // 添加节点
    assert(db.add_node("node2", "localhost", 8002));
    assert(db.add_node("node3", "localhost", 8003));
    
    // 创建分片
    assert(db.create_shard("shard1", "a", "m"));
    assert(db.create_shard("shard2", "m", "z"));
    
    // 开始事务
    auto tx = db.begin_transaction();
    assert(tx != nullptr);
    
    // 在事务中执行操作
    assert(tx->put("apple", "red_fruit"));
    assert(tx->put("banana", "yellow_fruit"));
    assert(tx->delete_key("old_key"));
    
    // 提交事务
    assert(tx->commit());
    
    // 验证数据
    auto response1 = db.get("apple");
    std::cout << "Transaction result - apple: " << (response1.success ? "found" : "not found") << std::endl;
    
    auto response2 = db.get("banana");
    std::cout << "Transaction result - banana: " << (response2.success ? "found" : "not found") << std::endl;
    
    db.shutdown();
    std::cout << "Distributed transactions test passed!" << std::endl;
}

void test_cluster_stats() {
    std::cout << "\n=== Testing Cluster Statistics ===" << std::endl;
    
    DistributedKVDB db;
    assert(db.initialize("node1", "localhost", 8001));
    
    // 添加节点
    assert(db.add_node("node2", "localhost", 8002));
    assert(db.add_node("node3", "localhost", 8003));
    
    // 创建分片
    assert(db.create_shard("shard1", "", "~"));
    
    // 执行一些操作
    db.put("key1", "value1");
    db.put("key2", "value2");
    db.get("key1");
    
    // 获取集群统计信息
    auto stats = db.get_cluster_stats();
    
    std::cout << "Cluster Statistics:" << std::endl;
    std::cout << "  Total nodes: " << stats.total_nodes << std::endl;
    std::cout << "  Healthy nodes: " << stats.healthy_nodes << std::endl;
    std::cout << "  Total shards: " << stats.total_shards << std::endl;
    std::cout << "  Failed shards: " << stats.failed_shards << std::endl;
    std::cout << "  Average load: " << stats.average_load << std::endl;
    std::cout << "  Total operations: " << stats.total_operations << std::endl;
    std::cout << "  Average latency: " << stats.average_latency_ms << " ms" << std::endl;
    
    std::cout << "Cluster healthy: " << (db.is_cluster_healthy() ? "Yes" : "No") << std::endl;
    
    db.shutdown();
    std::cout << "Cluster statistics test passed!" << std::endl;
}

void test_async_operations() {
    std::cout << "\n=== Testing Async Operations ===" << std::endl;
    
    DistributedKVDB db;
    assert(db.initialize("node1", "localhost", 8001));
    
    // 添加节点
    assert(db.add_node("node2", "localhost", 8002));
    assert(db.add_node("node3", "localhost", 8003));
    
    // 创建分片
    assert(db.create_shard("shard1", "", "~"));
    
    // 异步操作
    auto put_future = db.put_async("async_key", "async_value");
    auto get_future = db.get_async("async_key");
    
    // 等待结果
    auto put_result = put_future.get();
    auto get_result = get_future.get();
    
    std::cout << "Async put result: " << (put_result.success ? "success" : "failed") << std::endl;
    std::cout << "Async get result: " << (get_result.success ? "success" : "failed") << std::endl;
    
    // 异步扫描
    auto scan_future = db.scan_async("a", "z", 10);
    auto scan_result = scan_future.get();
    
    std::cout << "Async scan found " << scan_result.results.size() << " items" << std::endl;
    
    db.shutdown();
    std::cout << "Async operations test passed!" << std::endl;
}

int main() {
    std::cout << "Starting Distributed System Tests..." << std::endl;
    
    try {
        test_shard_management();
        test_load_balancing();
        test_failover();
        test_distributed_transactions();
        test_cluster_stats();
        test_async_operations();
        
        std::cout << "\n=== All Distributed System Tests Passed! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}