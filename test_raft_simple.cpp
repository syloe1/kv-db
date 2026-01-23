#include "src/raft/raft_node.h"
#include "src/raft/simple_raft_network.h"
#include "src/raft/simple_raft_state_machine.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== Simple Raft Test ===" << std::endl;
    
    try {
        // 创建单个节点进行基本测试
        RaftConfig config;
        config.node_id = "node0";
        config.cluster_nodes = {"node0"}; // 单节点集群
        config.listen_port = 8080;
        
        // 使用较短的超时时间进行测试
        config.election_timeout_min = std::chrono::milliseconds(100);
        config.election_timeout_max = std::chrono::milliseconds(200);
        config.heartbeat_interval = std::chrono::milliseconds(50);
        
        auto network = std::make_shared<SimpleRaftNetwork>("node0");
        auto state_machine = std::make_shared<SimpleRaftStateMachine>();
        
        RaftNode node(config, network, state_machine);
        
        std::cout << "Starting node..." << std::endl;
        if (!node.start()) {
            std::cout << "Failed to start node" << std::endl;
            return 1;
        }
        
        // 等待一段时间
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 检查状态
        RaftStats stats = node.get_statistics();
        std::cout << "Node state: ";
        switch (stats.state) {
            case RaftState::FOLLOWER: std::cout << "FOLLOWER"; break;
            case RaftState::CANDIDATE: std::cout << "CANDIDATE"; break;
            case RaftState::LEADER: std::cout << "LEADER"; break;
        }
        std::cout << std::endl;
        std::cout << "Term: " << stats.current_term << std::endl;
        std::cout << "Is leader: " << (node.is_leader() ? "YES" : "NO") << std::endl;
        
        // 如果是领导者，测试客户端请求
        if (node.is_leader()) {
            std::cout << "\nTesting client request..." << std::endl;
            
            ClientRequest request;
            request.request_id = "test1";
            request.command = "SET key1 value1";
            
            ClientResponse response = node.handle_client_request(request);
            std::cout << "Response result: " << static_cast<int>(response.result) << std::endl;
            std::cout << "Response data: " << response.response_data << std::endl;
        }
        
        std::cout << "\nStopping node..." << std::endl;
        node.stop();
        
        std::cout << "Test completed successfully!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}