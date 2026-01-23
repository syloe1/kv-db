#include "src/distributed/shard_manager.h"
#include "src/distributed/load_balancer.h"
#include "src/distributed/failover_manager.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

using namespace kvdb::distributed;

void demonstrate_sharding() {
    std::cout << "\n=== 分片演示 (Sharding Demo) ===" << std::endl;
    
    ShardManager sm;
    
    // 创建多个分片
    sm.create_shard("fruits", "a", "h");      // a-g 的键
    sm.create_shard("vegetables", "h", "p");  // h-o 的键  
    sm.create_shard("animals", "p", "~");     // p-z 的键
    
    std::cout << "创建了3个分片：" << std::endl;
    std::cout << "  - fruits: 键范围 a-g" << std::endl;
    std::cout << "  - vegetables: 键范围 h-o" << std::endl;
    std::cout << "  - animals: 键范围 p-z" << std::endl;
    
    // 测试键分片
    std::vector<std::string> test_keys = {
        "apple", "banana", "cherry",      // 应该在 fruits 分片
        "lettuce", "mushroom", "onion",   // 应该在 vegetables 分片
        "rabbit", "snake", "zebra"        // 应该在 animals 分片
    };
    
    std::cout << "\n键分片结果：" << std::endl;
    for (const auto& key : test_keys) {
        std::string shard = sm.get_shard_for_key(key);
        std::cout << "  " << key << " -> " << shard << std::endl;
    }
    
    // 添加副本节点
    std::vector<std::string> nodes = {"node1", "node2", "node3"};
    for (const auto& shard_id : {"fruits", "vegetables", "animals"}) {
        for (const auto& node : nodes) {
            sm.add_replica(shard_id, node);
        }
        sm.set_primary_node(shard_id, nodes[0]);
    }
    
    std::cout << "\n每个分片都有3个副本，主节点为 node1" << std::endl;
}

void demonstrate_load_balancing() {
    std::cout << "\n=== 负载均衡演示 (Load Balancing Demo) ===" << std::endl;
    
    LoadBalancer lb;
    
    // 添加节点
    NodeInfo node1("server1", "192.168.1.10", 8001);
    NodeInfo node2("server2", "192.168.1.11", 8002);
    NodeInfo node3("server3", "192.168.1.12", 8003);
    
    lb.add_node(node1);
    lb.add_node(node2);
    lb.add_node(node3);
    
    std::cout << "添加了3个服务器节点" << std::endl;
    
    // 演示不同的负载均衡策略
    std::cout << "\n1. 轮询策略 (Round Robin):" << std::endl;
    lb.set_strategy(LoadBalanceStrategy::ROUND_ROBIN);
    for (int i = 0; i < 6; ++i) {
        std::string selected = lb.select_node();
        std::cout << "  请求 " << (i+1) << " -> " << selected << std::endl;
    }
    
    std::cout << "\n2. 一致性哈希策略 (Consistent Hash):" << std::endl;
    lb.set_strategy(LoadBalanceStrategy::CONSISTENT_HASH);
    std::vector<std::string> user_keys = {"user123", "user456", "user789", "user101", "user202"};
    for (const auto& key : user_keys) {
        std::string selected = lb.select_node_for_key(key);
        std::cout << "  " << key << " -> " << selected << std::endl;
    }
    
    // 模拟负载变化
    std::cout << "\n3. 模拟负载变化:" << std::endl;
    lb.update_node_load("server1", 0.8);  // 高负载
    lb.update_node_load("server2", 0.3);  // 低负载
    lb.update_node_load("server3", 0.5);  // 中等负载
    
    lb.set_strategy(LoadBalanceStrategy::LEAST_CONNECTIONS);
    std::cout << "  切换到最少连接策略，选择负载最低的节点:" << std::endl;
    for (int i = 0; i < 3; ++i) {
        std::string selected = lb.select_node();
        std::cout << "    请求 " << (i+1) << " -> " << selected << std::endl;
    }
}

void demonstrate_replication() {
    std::cout << "\n=== 副本管理演示 (Replication Demo) ===" << std::endl;
    
    ShardManager sm;
    LoadBalancer lb;
    
    // 创建分片
    sm.create_shard("user_data", "", "~");
    
    // 添加节点
    std::vector<std::string> nodes = {"primary", "replica1", "replica2"};
    for (const auto& node : nodes) {
        NodeInfo node_info(node, "localhost", 8000 + (&node - &nodes[0]));
        lb.add_node(node_info);
        sm.add_replica("user_data", node);
    }
    
    sm.set_primary_node("user_data", "primary");
    
    std::cout << "创建了用户数据分片，包含：" << std::endl;
    std::cout << "  - 主节点: primary" << std::endl;
    std::cout << "  - 副本节点: replica1, replica2" << std::endl;
    
    auto replicas = sm.get_replica_nodes("user_data");
    std::string primary = sm.get_primary_node("user_data");
    
    std::cout << "\n分片配置：" << std::endl;
    std::cout << "  主节点: " << primary << std::endl;
    std::cout << "  副本数量: " << replicas.size() << std::endl;
    std::cout << "  副本节点: ";
    for (const auto& replica : replicas) {
        std::cout << replica << " ";
    }
    std::cout << std::endl;
    
    // 模拟副本选择
    auto selected_replicas = lb.select_replica_nodes(2);
    std::cout << "\n为新数据选择的副本节点: ";
    for (const auto& replica : selected_replicas) {
        std::cout << replica << " ";
    }
    std::cout << std::endl;
}

void demonstrate_failover() {
    std::cout << "\n=== 故障转移演示 (Failover Demo) ===" << std::endl;
    
    ShardManager sm;
    LoadBalancer lb;
    
    // 设置集群
    std::vector<std::string> nodes = {"master", "slave1", "slave2"};
    for (const auto& node : nodes) {
        NodeInfo node_info(node, "localhost", 8000 + (&node - &nodes[0]));
        lb.add_node(node_info);
    }
    
    sm.create_shard("critical_data", "", "~");
    for (const auto& node : nodes) {
        sm.add_replica("critical_data", node);
    }
    sm.set_primary_node("critical_data", "master");
    
    FailoverManager fm(&sm, &lb);
    fm.set_auto_failover(true);
    fm.set_failover_timeout(5);  // 5秒超时
    
    std::cout << "初始集群状态：" << std::endl;
    std::cout << "  主节点: master" << std::endl;
    std::cout << "  备份节点: slave1, slave2" << std::endl;
    std::cout << "  自动故障转移: 启用" << std::endl;
    
    // 启动故障转移监控
    fm.start_monitoring();
    std::cout << "\n故障转移监控已启动" << std::endl;
    
    // 模拟主节点故障
    std::cout << "\n模拟主节点故障..." << std::endl;
    lb.mark_node_unhealthy("master");
    
    // 等待故障检测
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 检查故障转移状态
    auto state = fm.get_failover_state();
    std::string status = fm.get_failover_status();
    std::cout << "故障转移状态: " << static_cast<int>(state) << std::endl;
    std::cout << "状态信息: " << status << std::endl;
    
    // 执行故障转移
    bool promoted = fm.promote_replica_to_primary("critical_data", "slave1");
    if (promoted) {
        std::cout << "成功将 slave1 提升为新的主节点" << std::endl;
    }
    
    // 显示新的集群状态
    std::string new_primary = sm.get_primary_node("critical_data");
    std::cout << "新的主节点: " << new_primary << std::endl;
    
    // 模拟主节点恢复
    std::cout << "\n模拟原主节点恢复..." << std::endl;
    lb.mark_node_healthy("master");
    fm.recover_node("master");
    std::cout << "master 节点已恢复，重新加入集群" << std::endl;
    
    fm.stop_monitoring();
    std::cout << "故障转移监控已停止" << std::endl;
}

void demonstrate_cluster_stats() {
    std::cout << "\n=== 集群统计演示 (Cluster Statistics Demo) ===" << std::endl;
    
    ShardManager sm;
    LoadBalancer lb;
    
    // 创建一个模拟集群
    std::vector<std::string> nodes = {"web1", "web2", "web3", "db1", "db2"};
    for (const auto& node : nodes) {
        NodeInfo node_info(node, "10.0.0." + std::to_string(&node - &nodes[0] + 1), 8000);
        lb.add_node(node_info);
    }
    
    // 创建多个分片
    sm.create_shard("users", "a", "m");
    sm.create_shard("orders", "m", "z");
    sm.create_shard("products", "0", "9");
    
    // 分配副本
    for (const auto& shard : {"users", "orders", "products"}) {
        for (int i = 0; i < 3; ++i) {
            sm.add_replica(shard, nodes[i]);
        }
        sm.set_primary_node(shard, nodes[0]);
    }
    
    // 模拟负载
    lb.update_node_load("web1", 0.7);
    lb.update_node_load("web2", 0.5);
    lb.update_node_load("web3", 0.3);
    lb.update_node_load("db1", 0.8);
    lb.update_node_load("db2", 0.4);
    
    std::cout << "集群概览：" << std::endl;
    std::cout << "  总节点数: " << lb.get_node_count() << std::endl;
    std::cout << "  健康节点数: " << lb.get_healthy_node_count() << std::endl;
    std::cout << "  平均负载: " << std::fixed << std::setprecision(2) << lb.get_average_load() << std::endl;
    
    auto all_shards = sm.get_all_shards();
    std::cout << "  总分片数: " << all_shards.size() << std::endl;
    
    std::cout << "\n分片详情：" << std::endl;
    for (const auto& shard : all_shards) {
        std::cout << "  " << shard.shard_id << ":" << std::endl;
        std::cout << "    键范围: [" << shard.start_key << ", " << shard.end_key << ")" << std::endl;
        std::cout << "    主节点: " << shard.primary_node << std::endl;
        std::cout << "    副本数: " << shard.replica_nodes.size() << std::endl;
    }
    
    std::cout << "\n节点负载分布：" << std::endl;
    auto all_nodes = lb.get_all_nodes();
    for (const auto& node : all_nodes) {
        std::cout << "  " << node.node_id << " (" << node.host << "): " 
                  << std::fixed << std::setprecision(1) << (node.load_factor * 100) << "%" << std::endl;
    }
}

int main() {
    std::cout << "分布式键值数据库系统演示" << std::endl;
    std::cout << "==============================" << std::endl;
    
    try {
        demonstrate_sharding();
        demonstrate_load_balancing();
        demonstrate_replication();
        demonstrate_failover();
        demonstrate_cluster_stats();
        
        std::cout << "\n=== 演示完成 ===" << std::endl;
        std::cout << "分布式系统的四个核心功能已全部演示：" << std::endl;
        std::cout << "✓ 分片 (Sharding) - 数据水平分割" << std::endl;
        std::cout << "✓ 副本 (Replication) - 数据冗余备份" << std::endl;
        std::cout << "✓ 负载均衡 (Load Balancing) - 请求分发" << std::endl;
        std::cout << "✓ 故障转移 (Failover) - 自动故障恢复" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "演示过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}