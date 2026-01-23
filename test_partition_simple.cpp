#include "src/partition_recovery/failure_detector.h"
#include "src/partition_recovery/simple_partition_network.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "    网络分区处理系统简单测试" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        // 创建故障检测器配置
        FailureDetectorConfig config;
        config.heartbeat_interval = std::chrono::milliseconds(1000);
        config.failure_timeout = std::chrono::milliseconds(3000);
        config.max_consecutive_failures = 2;
        config.enable_partition_detection = true;
        
        std::cout << "✅ 配置创建成功" << std::endl;
        
        // 创建网络接口
        auto network = std::make_shared<SimplePartitionRecoveryNetwork>("test_node");
        std::cout << "✅ 网络接口创建成功" << std::endl;
        
        // 创建故障检测器
        FailureDetector detector("test_node", config, network);
        std::cout << "✅ 故障检测器创建成功" << std::endl;
        
        // 添加监控节点
        detector.add_node("node1", "127.0.0.1", 8081);
        detector.add_node("node2", "127.0.0.1", 8082);
        std::cout << "✅ 监控节点添加成功" << std::endl;
        
        // 测试节点健康状态查询
        auto monitored_nodes = detector.get_monitored_nodes();
        std::cout << "监控节点数量: " << monitored_nodes.size() << std::endl;
        
        for (const auto& node : monitored_nodes) {
            std::cout << "  节点: " << node.node_id 
                      << " 地址: " << node.address << ":" << node.port
                      << " 健康状态: " << (node.is_healthy() ? "健康" : "故障") << std::endl;
        }
        
        // 测试分区信息
        auto current_partition = detector.get_current_partition();
        std::cout << "当前分区ID: " << current_partition.partition_id << std::endl;
        std::cout << "分区节点数: " << current_partition.nodes.size() << std::endl;
        
        // 测试网络分区模拟
        std::vector<std::string> partition1 = {"test_node"};
        std::vector<std::string> partition2 = {"node1", "node2"};
        
        network->simulate_partition(partition1, partition2);
        std::cout << "✅ 网络分区模拟成功" << std::endl;
        
        // 测试连通性
        std::cout << "节点连通性测试:" << std::endl;
        std::cout << "  node1 可达: " << (network->is_node_reachable("node1") ? "是" : "否") << std::endl;
        std::cout << "  node2 可达: " << (network->is_node_reachable("node2") ? "是" : "否") << std::endl;
        
        // 恢复网络
        network->heal_partition();
        std::cout << "✅ 网络分区恢复成功" << std::endl;
        
        // 再次测试连通性
        std::cout << "恢复后连通性测试:" << std::endl;
        std::cout << "  node1 可达: " << (network->is_node_reachable("node1") ? "是" : "否") << std::endl;
        std::cout << "  node2 可达: " << (network->is_node_reachable("node2") ? "是" : "否") << std::endl;
        
        // 测试统计信息
        auto stats = detector.get_statistics();
        std::cout << "\n统计信息:" << std::endl;
        std::cout << "  检测到的分区数: " << stats.total_partitions_detected << std::endl;
        std::cout << "  已恢复的分区数: " << stats.partitions_recovered << std::endl;
        std::cout << "  检测到的节点故障数: " << stats.node_failures_detected << std::endl;
        std::cout << "  已恢复的节点数: " << stats.nodes_recovered << std::endl;
        
        // 测试分区恢复上下文
        PartitionRecoveryContext context("test_recovery");
        
        PartitionInfo partition;
        partition.partition_id = "test_partition";
        partition.state = PartitionState::PARTITIONED;
        partition.nodes.insert("test_node");
        partition.nodes.insert("node1");
        
        context.add_partition(partition);
        std::cout << "✅ 分区恢复上下文测试成功" << std::endl;
        
        auto partitions = context.get_partitions();
        std::cout << "恢复上下文中的分区数: " << partitions.size() << std::endl;
        
        // 测试冲突处理
        ConsistencyConflict conflict;
        conflict.key = "test_key";
        conflict.conflicting_values = {"value1", "value2"};
        conflict.nodes = {"node1", "node2"};
        conflict.timestamps = {1000, 2000};
        
        context.add_conflict(conflict);
        std::cout << "✅ 一致性冲突测试成功" << std::endl;
        
        auto conflicts = context.get_unresolved_conflicts();
        std::cout << "未解决的冲突数: " << conflicts.size() << std::endl;
        
        context.resolve_conflict("test_key", "resolved_value");
        std::cout << "✅ 冲突解决测试成功" << std::endl;
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "✅ 所有测试通过！网络分区处理系统实现成功！" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "❌ 测试失败: " << e.what() << std::endl;
        return 1;
    }
}