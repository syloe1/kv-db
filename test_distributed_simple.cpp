#include "src/distributed/shard_manager.h"
#include "src/distributed/load_balancer.h"
#include "src/distributed/failover_manager.h"
#include <iostream>
#include <cassert>

using namespace kvdb::distributed;

void test_shard_manager() {
    std::cout << "\n=== Testing Shard Manager ===" << std::endl;
    
    ShardManager sm;
    
    // 创建分片
    assert(sm.create_shard("shard1", "a", "m"));
    assert(sm.create_shard("shard2", "m", "z"));
    
    // 测试键分片
    std::string shard1 = sm.get_shard_for_key("apple");
    std::string shard2 = sm.get_shard_for_key("zebra");
    
    std::cout << "Key 'apple' maps to shard: " << shard1 << std::endl;
    std::cout << "Key 'zebra' maps to shard: " << shard2 << std::endl;
    
    // 添加副本
    assert(sm.add_replica("shard1", "node1"));
    assert(sm.add_replica("shard1", "node2"));
    assert(sm.set_primary_node("shard1", "node1"));
    
    auto replicas = sm.get_replica_nodes("shard1");
    std::cout << "Shard1 has " << replicas.size() << " replicas" << std::endl;
    
    std::string primary = sm.get_primary_node("shard1");
    std::cout << "Shard1 primary node: " << primary << std::endl;
    
    std::cout << "Shard Manager test passed!" << std::endl;
}

void test_load_balancer() {
    std::cout << "\n=== Testing Load Balancer ===" << std::endl;
    
    LoadBalancer lb(LoadBalanceStrategy::ROUND_ROBIN);
    
    // 添加节点
    NodeInfo node1("node1", "localhost", 8001);
    NodeInfo node2("node2", "localhost", 8002);
    NodeInfo node3("node3", "localhost", 8003);
    
    assert(lb.add_node(node1));
    assert(lb.add_node(node2));
    assert(lb.add_node(node3));
    
    std::cout << "Added 3 nodes to load balancer" << std::endl;
    
    // 测试轮询选择
    std::cout << "Round-robin selection:" << std::endl;
    for (int i = 0; i < 6; ++i) {
        std::string node = lb.select_node();
        std::cout << "  Request " << (i+1) << " -> " << node << std::endl;
    }
    
    // 测试一致性哈希
    lb.set_strategy(LoadBalanceStrategy::CONSISTENT_HASH);
    std::string node_for_key1 = lb.select_node_for_key("test_key_1");
    std::string node_for_key2 = lb.select_node_for_key("test_key_1");
    assert(node_for_key1 == node_for_key2);
    
    std::cout << "Consistent hash: key 'test_key_1' -> " << node_for_key1 << std::endl;
    
    // 测试副本选择
    auto replicas = lb.select_replica_nodes(2);
    std::cout << "Selected " << replicas.size() << " replica nodes" << std::endl;
    
    // 测试统计信息
    std::cout << "Total nodes: " << lb.get_node_count() << std::endl;
    std::cout << "Healthy nodes: " << lb.get_healthy_node_count() << std::endl;
    
    std::cout << "Load Balancer test passed!" << std::endl;
}

void test_failover_manager() {
    std::cout << "\n=== Testing Failover Manager ===" << std::endl;
    
    ShardManager sm;
    LoadBalancer lb;
    
    // 添加节点和分片
    NodeInfo node1("node1", "localhost", 8001);
    NodeInfo node2("node2", "localhost", 8002);
    lb.add_node(node1);
    lb.add_node(node2);
    
    sm.create_shard("shard1", "", "~");
    sm.add_replica("shard1", "node1");
    sm.add_replica("shard1", "node2");
    sm.set_primary_node("shard1", "node1");
    
    FailoverManager fm(&sm, &lb);
    
    // 启动监控
    assert(fm.start_monitoring());
    std::cout << "Failover monitoring started" << std::endl;
    
    // 模拟节点故障
    lb.mark_node_unhealthy("node1");
    std::cout << "Marked node1 as unhealthy" << std::endl;
    
    // 检查故障检测
    bool failure_detected = fm.detect_node_failure("node1");
    std::cout << "Node failure detected: " << (failure_detected ? "Yes" : "No") << std::endl;
    
    // 测试故障转移
    bool promoted = fm.promote_replica_to_primary("shard1", "node2");
    std::cout << "Replica promotion: " << (promoted ? "Success" : "Failed") << std::endl;
    
    // 恢复节点
    lb.mark_node_healthy("node1");
    fm.recover_node("node1");
    std::cout << "Node1 recovered" << std::endl;
    
    // 停止监控
    assert(fm.stop_monitoring());
    std::cout << "Failover monitoring stopped" << std::endl;
    
    std::cout << "Failover Manager test passed!" << std::endl;
}

int main() {
    std::cout << "Starting Distributed Components Tests..." << std::endl;
    
    try {
        test_shard_manager();
        test_load_balancer();
        test_failover_manager();
        
        std::cout << "\n=== All Distributed Components Tests Passed! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}