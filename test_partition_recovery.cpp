#include "src/partition_recovery/failure_detector.h"
#include "src/partition_recovery/simple_partition_network.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>

class PartitionRecoveryTest {
public:
    PartitionRecoveryTest() {
        // 创建故障检测器配置
        FailureDetectorConfig config;
        config.heartbeat_interval = std::chrono::milliseconds(500);
        config.failure_timeout = std::chrono::milliseconds(2000);
        config.recovery_timeout = std::chrono::milliseconds(10000);
        config.max_consecutive_failures = 2;
        config.enable_partition_detection = true;
        config.enable_auto_recovery = true;
        
        // 创建节点
        create_nodes(config);
        
        // 设置网络连接
        setup_network();
    }
    
    ~PartitionRecoveryTest() {
        stop_all_nodes();
    }
    
    void create_nodes(const FailureDetectorConfig& config) {
        for (int i = 0; i < 5; i++) {
            std::string node_id = "node" + std::to_string(i);
            
            auto network = std::make_shared<SimplePartitionRecoveryNetwork>(node_id);
            auto detector = std::make_shared<FailureDetector>(node_id, config, network);
            
            // 设置回调函数
            detector->set_failure_callback([this, node_id](const std::string& failed_node, FailureType type) {
                on_node_failure(node_id, failed_node, type);
            });
            
            detector->set_recovery_callback([this, node_id](const std::string& recovered_node) {
                on_node_recovery(node_id, recovered_node);
            });
            
            detector->set_partition_callback([this, node_id](const PartitionInfo& partition) {
                on_partition_detected(node_id, partition);
            });
            
            networks_.push_back(network);
            detectors_.push_back(detector);
        }
    }
    
    void setup_network() {
        // 为每个节点添加其他节点作为对等节点
        for (size_t i = 0; i < detectors_.size(); i++) {
            for (size_t j = 0; j < detectors_.size(); j++) {
                if (i != j) {
                    std::string peer_id = "node" + std::to_string(j);
                    std::string peer_address = "127.0.0.1";
                    uint16_t peer_port = 8080 + j;
                    
                    detectors_[i]->add_node(peer_id, peer_address, peer_port);
                    networks_[i]->add_node(peer_id, peer_address, peer_port);
                }
            }
        }
    }
    
    bool start_all_nodes() {
        std::cout << "\n=== Starting Partition Recovery System ===" << std::endl;
        
        for (size_t i = 0; i < detectors_.size(); i++) {
            if (!detectors_[i]->start()) {
                std::cout << "Failed to start detector " << i << std::endl;
                return false;
            }
        }
        
        // 等待一下让所有组件启动完成
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        std::cout << "All nodes started successfully" << std::endl;
        return true;
    }
    
    void stop_all_nodes() {
        std::cout << "\n=== Stopping Partition Recovery System ===" << std::endl;
        
        for (auto& detector : detectors_) {
            detector->stop();
        }
        
        std::cout << "All nodes stopped" << std::endl;
    }
    
    void test_normal_operation() {
        std::cout << "\n=== Testing Normal Operation ===" << std::endl;
        
        // 等待几个心跳周期
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        // 检查所有节点的健康状态
        for (size_t i = 0; i < detectors_.size(); i++) {
            std::string node_id = "node" + std::to_string(i);
            auto healthy_nodes = detectors_[i]->get_healthy_nodes();
            
            std::cout << "Node " << node_id << " sees " << healthy_nodes.size() 
                      << " healthy nodes: ";
            for (const auto& healthy_node : healthy_nodes) {
                std::cout << healthy_node << " ";
            }
            std::cout << std::endl;
        }
        
        // 检查是否检测到分区
        bool any_partitioned = false;
        for (size_t i = 0; i < detectors_.size(); i++) {
            if (detectors_[i]->is_partitioned()) {
                any_partitioned = true;
                break;
            }
        }
        
        if (any_partitioned) {
            std::cout << "✗ Unexpected partition detected during normal operation" << std::endl;
        } else {
            std::cout << "✓ Normal operation: No partitions detected" << std::endl;
        }
    }
    
    void test_network_partition() {
        std::cout << "\n=== Testing Network Partition ===" << std::endl;
        
        // 模拟网络分区：node0, node1 vs node2, node3, node4
        std::vector<std::string> partition1 = {"node0", "node1"};
        std::vector<std::string> partition2 = {"node2", "node3", "node4"};
        
        std::cout << "Simulating network partition..." << std::endl;
        std::cout << "Partition 1: ";
        for (const auto& node : partition1) {
            std::cout << node << " ";
        }
        std::cout << std::endl;
        
        std::cout << "Partition 2: ";
        for (const auto& node : partition2) {
            std::cout << node << " ";
        }
        std::cout << std::endl;
        
        // 在所有网络实例上模拟分区
        for (auto& network : networks_) {
            network->simulate_partition(partition1, partition2);
        }
        
        // 等待分区检测
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 检查分区检测结果
        check_partition_detection();
    }
    
    void test_partition_recovery() {
        std::cout << "\n=== Testing Partition Recovery ===" << std::endl;
        
        std::cout << "Healing network partition..." << std::endl;
        
        // 恢复网络分区
        for (auto& network : networks_) {
            network->heal_partition();
        }
        
        // 等待恢复检测
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 检查恢复结果
        check_partition_recovery();
    }
    
    void test_node_failure() {
        std::cout << "\n=== Testing Node Failure ===" << std::endl;
        
        // 停止node4来模拟节点故障
        std::cout << "Stopping node4 to simulate node failure..." << std::endl;
        detectors_[4]->stop();
        
        // 等待故障检测
        std::this_thread::sleep_for(std::chrono::seconds(4));
        
        // 检查故障检测结果
        for (size_t i = 0; i < 4; i++) { // 不包括已停止的node4
            std::string node_id = "node" + std::to_string(i);
            auto failed_nodes = detectors_[i]->get_failed_nodes();
            
            std::cout << "Node " << node_id << " detected " << failed_nodes.size() 
                      << " failed nodes: ";
            for (const auto& failed_node : failed_nodes) {
                std::cout << failed_node << " ";
            }
            std::cout << std::endl;
        }
    }
    
    void test_node_recovery() {
        std::cout << "\n=== Testing Node Recovery ===" << std::endl;
        
        // 重启node4
        std::cout << "Restarting node4..." << std::endl;
        if (detectors_[4]->start()) {
            std::cout << "Node4 restarted successfully" << std::endl;
            
            // 等待恢复检测
            std::this_thread::sleep_for(std::chrono::seconds(3));
            
            // 检查恢复结果
            for (size_t i = 0; i < detectors_.size(); i++) {
                std::string node_id = "node" + std::to_string(i);
                auto healthy_nodes = detectors_[i]->get_healthy_nodes();
                
                std::cout << "Node " << node_id << " sees " << healthy_nodes.size() 
                          << " healthy nodes after recovery" << std::endl;
            }
        } else {
            std::cout << "Failed to restart node4" << std::endl;
        }
    }
    
    void check_partition_detection() {
        std::cout << "\nChecking partition detection results:" << std::endl;
        
        for (size_t i = 0; i < detectors_.size(); i++) {
            std::string node_id = "node" + std::to_string(i);
            
            if (detectors_[i]->is_partitioned()) {
                auto partitions = detectors_[i]->get_detected_partitions();
                std::cout << "✓ Node " << node_id << " detected " << partitions.size() 
                          << " partitions" << std::endl;
                
                for (const auto& partition : partitions) {
                    std::cout << "  Partition " << partition.partition_id << ": ";
                    for (const auto& node : partition.nodes) {
                        std::cout << node << " ";
                    }
                    std::cout << "(majority: " << partition.is_majority_partition(5) << ")" << std::endl;
                }
            } else {
                std::cout << "✗ Node " << node_id << " did not detect partition" << std::endl;
            }
        }
    }
    
    void check_partition_recovery() {
        std::cout << "\nChecking partition recovery results:" << std::endl;
        
        bool all_recovered = true;
        for (size_t i = 0; i < detectors_.size(); i++) {
            std::string node_id = "node" + std::to_string(i);
            
            if (!detectors_[i]->is_partitioned()) {
                std::cout << "✓ Node " << node_id << " recovered from partition" << std::endl;
            } else {
                std::cout << "✗ Node " << node_id << " still partitioned" << std::endl;
                all_recovered = false;
            }
        }
        
        if (all_recovered) {
            std::cout << "✓ All nodes recovered from partition successfully" << std::endl;
        }
    }
    
    void print_statistics() {
        std::cout << "\n=== System Statistics ===" << std::endl;
        
        for (size_t i = 0; i < detectors_.size(); i++) {
            std::string node_id = "node" + std::to_string(i);
            auto stats = detectors_[i]->get_statistics();
            
            std::cout << "Node " << node_id << " Statistics:" << std::endl;
            std::cout << "  Partitions Detected: " << stats.total_partitions_detected << std::endl;
            std::cout << "  Partitions Recovered: " << stats.partitions_recovered << std::endl;
            std::cout << "  Node Failures Detected: " << stats.node_failures_detected << std::endl;
            std::cout << "  Nodes Recovered: " << stats.nodes_recovered << std::endl;
            std::cout << "  Recovery Rate: " << (stats.partition_recovery_rate * 100) << "%" << std::endl;
            std::cout << std::endl;
        }
    }
    
    void run_comprehensive_test() {
        std::cout << "========================================" << std::endl;
        std::cout << "    网络分区处理系统综合测试" << std::endl;
        std::cout << "========================================" << std::endl;
        
        if (!start_all_nodes()) {
            std::cout << "Failed to start system" << std::endl;
            return;
        }
        
        // 运行各种测试
        test_normal_operation();
        test_network_partition();
        test_partition_recovery();
        test_node_failure();
        test_node_recovery();
        
        // 打印统计信息
        print_statistics();
        
        std::cout << "\n=== Test Completed ===" << std::endl;
    }

private:
    std::vector<std::shared_ptr<SimplePartitionRecoveryNetwork>> networks_;
    std::vector<std::shared_ptr<FailureDetector>> detectors_;
    
    // 回调处理
    void on_node_failure(const std::string& detector_node, const std::string& failed_node, FailureType type) {
        std::cout << "[" << detector_node << "] Detected failure of " << failed_node 
                  << " (type: " << static_cast<int>(type) << ")" << std::endl;
    }
    
    void on_node_recovery(const std::string& detector_node, const std::string& recovered_node) {
        std::cout << "[" << detector_node << "] Detected recovery of " << recovered_node << std::endl;
    }
    
    void on_partition_detected(const std::string& detector_node, const PartitionInfo& partition) {
        std::cout << "[" << detector_node << "] Detected partition " << partition.partition_id 
                  << " with " << partition.nodes.size() << " nodes" << std::endl;
    }
};

int main() {
    try {
        PartitionRecoveryTest test;
        test.run_comprehensive_test();
        
        std::cout << "\nAll partition recovery tests completed!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}