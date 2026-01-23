#include "src/raft/raft_node.h"
#include "src/raft/simple_raft_network.h"
#include "src/raft/simple_raft_state_machine.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <cassert>

class RaftClusterTest {
public:
    RaftClusterTest(size_t cluster_size) : cluster_size_(cluster_size) {
        // 创建集群节点ID列表
        for (size_t i = 0; i < cluster_size_; i++) {
            std::string node_id = "node" + std::to_string(i);
            node_ids_.push_back(node_id);
        }
        
        // 创建节点
        for (size_t i = 0; i < cluster_size_; i++) {
            create_node(i);
        }
        
        // 配置节点间的网络连接
        setup_network();
    }
    
    ~RaftClusterTest() {
        stop_all_nodes();
    }
    
    void start_all_nodes() {
        std::cout << "\n=== Starting Raft Cluster ===" << std::endl;
        
        for (auto& node : nodes_) {
            if (!node->start()) {
                std::cout << "Failed to start node" << std::endl;
                return;
            }
        }
        
        std::cout << "All nodes started successfully" << std::endl;
    }
    
    void stop_all_nodes() {
        std::cout << "\n=== Stopping Raft Cluster ===" << std::endl;
        
        for (auto& node : nodes_) {
            node->stop();
        }
        
        std::cout << "All nodes stopped" << std::endl;
    }
    
    void wait_for_leader_election() {
        std::cout << "\n=== Waiting for Leader Election ===" << std::endl;
        
        std::string leader_id;
        int attempts = 0;
        const int max_attempts = 50; // 5秒超时
        
        while (attempts < max_attempts) {
            for (auto& node : nodes_) {
                if (node->is_leader()) {
                    leader_id = node->get_leader_id();
                    break;
                }
            }
            
            if (!leader_id.empty()) {
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            attempts++;
        }
        
        if (leader_id.empty()) {
            std::cout << "ERROR: No leader elected within timeout" << std::endl;
        } else {
            std::cout << "Leader elected: " << leader_id << std::endl;
            
            // 验证所有节点都认识这个领导者
            for (size_t i = 0; i < nodes_.size(); i++) {
                std::string node_leader = nodes_[i]->get_leader_id();
                std::cout << "Node " << node_ids_[i] << " sees leader: " 
                          << (node_leader.empty() ? "NONE" : node_leader) << std::endl;
            }
        }
    }
    
    void test_client_requests() {
        std::cout << "\n=== Testing Client Requests ===" << std::endl;
        
        // 找到领导者
        RaftNode* leader = nullptr;
        std::string leader_id;
        
        for (size_t i = 0; i < nodes_.size(); i++) {
            if (nodes_[i]->is_leader()) {
                leader = nodes_[i].get();
                leader_id = node_ids_[i];
                break;
            }
        }
        
        if (!leader) {
            std::cout << "ERROR: No leader found for client requests" << std::endl;
            return;
        }
        
        std::cout << "Sending client requests to leader: " << leader_id << std::endl;
        
        // 测试SET命令
        std::vector<std::pair<std::string, std::string>> test_data = {
            {"key1", "value1"},
            {"key2", "value2"},
            {"key3", "value3"},
            {"name", "RaftDB"},
            {"version", "1.0"}
        };
        
        for (const auto& pair : test_data) {
            ClientRequest request;
            request.request_id = "req_" + pair.first;
            request.command = "SET " + pair.first + " " + pair.second;
            
            std::cout << "Sending: " << request.command << std::endl;
            
            ClientResponse response = leader->handle_client_request(request);
            
            std::cout << "Response: " << static_cast<int>(response.result);
            if (!response.response_data.empty()) {
                std::cout << " - " << response.response_data;
            }
            std::cout << std::endl;
            
            // 等待一下让日志复制
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 测试GET命令
        std::cout << "\nTesting GET commands:" << std::endl;
        for (const auto& pair : test_data) {
            ClientRequest request;
            request.request_id = "get_" + pair.first;
            request.command = "GET " + pair.first;
            
            std::cout << "Sending: " << request.command << std::endl;
            
            ClientResponse response = leader->handle_client_request(request);
            
            std::cout << "Response: " << static_cast<int>(response.result);
            if (!response.response_data.empty()) {
                std::cout << " - " << response.response_data;
            }
            std::cout << std::endl;
        }
    }
    
    void print_cluster_status() {
        std::cout << "\n=== Cluster Status ===" << std::endl;
        
        for (size_t i = 0; i < nodes_.size(); i++) {
            RaftStats stats = nodes_[i]->get_statistics();
            
            std::cout << "Node " << node_ids_[i] << ":" << std::endl;
            std::cout << "  State: ";
            switch (stats.state) {
                case RaftState::FOLLOWER: std::cout << "FOLLOWER"; break;
                case RaftState::CANDIDATE: std::cout << "CANDIDATE"; break;
                case RaftState::LEADER: std::cout << "LEADER"; break;
            }
            std::cout << std::endl;
            
            std::cout << "  Term: " << stats.current_term << std::endl;
            std::cout << "  Leader: " << (stats.leader_id.empty() ? "NONE" : stats.leader_id) << std::endl;
            std::cout << "  Log Length: " << stats.log_length << std::endl;
            std::cout << "  Commit Index: " << stats.commit_index << std::endl;
            std::cout << "  Last Applied: " << stats.last_applied << std::endl;
            std::cout << "  Elections Started: " << stats.elections_started << std::endl;
            std::cout << "  Elections Won: " << stats.elections_won << std::endl;
            std::cout << "  Votes Received: " << stats.votes_received << std::endl;
            std::cout << std::endl;
        }
    }
    
    void run_comprehensive_test() {
        std::cout << "========================================" << std::endl;
        std::cout << "         Raft Implementation Test      " << std::endl;
        std::cout << "========================================" << std::endl;
        
        start_all_nodes();
        
        // 等待领导者选举
        wait_for_leader_election();
        
        // 打印初始状态
        print_cluster_status();
        
        // 测试客户端请求
        test_client_requests();
        
        // 等待一段时间让所有操作完成
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 打印最终状态
        print_cluster_status();
        
        std::cout << "\n=== Test Completed ===" << std::endl;
    }

private:
    size_t cluster_size_;
    std::vector<std::string> node_ids_;
    std::vector<std::shared_ptr<RaftNode>> nodes_;
    std::vector<std::shared_ptr<SimpleRaftNetwork>> networks_;
    std::vector<std::shared_ptr<SimpleRaftStateMachine>> state_machines_;
    
    void create_node(size_t index) {
        std::string node_id = node_ids_[index];
        
        // 创建配置
        RaftConfig config;
        config.node_id = node_id;
        config.cluster_nodes = node_ids_;
        config.listen_port = 8080 + index;
        
        // 使用较短的超时时间进行测试
        config.election_timeout_min = std::chrono::milliseconds(150);
        config.election_timeout_max = std::chrono::milliseconds(300);
        config.heartbeat_interval = std::chrono::milliseconds(50);
        
        // 创建网络和状态机
        auto network = std::make_shared<SimpleRaftNetwork>(node_id);
        auto state_machine = std::make_shared<SimpleRaftStateMachine>();
        
        // 创建节点
        auto node = std::make_shared<RaftNode>(config, network, state_machine);
        
        nodes_.push_back(node);
        networks_.push_back(network);
        state_machines_.push_back(state_machine);
    }
    
    void setup_network() {
        // 为每个节点添加其他节点作为对等节点
        for (size_t i = 0; i < nodes_.size(); i++) {
            for (size_t j = 0; j < nodes_.size(); j++) {
                if (i != j) {
                    std::string peer_address = "127.0.0.1";
                    uint16_t peer_port = 8080 + j;
                    networks_[i]->add_peer(node_ids_[j], peer_address, peer_port);
                }
            }
        }
    }
};

int main() {
    try {
        // 创建3节点集群进行测试
        RaftClusterTest test(3);
        
        // 运行综合测试
        test.run_comprehensive_test();
        
        std::cout << "\nAll tests completed successfully!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}