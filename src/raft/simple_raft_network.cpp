#include "simple_raft_network.h"
#include <iostream>
#include <thread>
#include <random>

// 静态成员初始化
std::mutex SimpleRaftNetwork::global_networks_mutex_;
std::unordered_map<std::string, SimpleRaftNetwork*> SimpleRaftNetwork::global_networks_;

SimpleRaftNetwork::SimpleRaftNetwork(const std::string& node_id)
    : node_id_(node_id), running_(false) {
    register_network(node_id_, this);
}

SimpleRaftNetwork::~SimpleRaftNetwork() {
    stop();
    unregister_network(node_id_);
}

bool SimpleRaftNetwork::send_message(const std::string& to, const RaftMessage& message) {
    if (!running_) {
        return false;
    }
    
    // 模拟网络延迟和丢包
    if (should_drop_message()) {
        std::cout << "Network: Message from " << node_id_ << " to " << to << " dropped" << std::endl;
        return false;
    }
    
    simulate_network_delay();
    
    // 查找目标节点的网络实例
    SimpleRaftNetwork* target_network = get_network(to);
    if (!target_network) {
        std::cout << "Network: Target node " << to << " not found" << std::endl;
        return false;
    }
    
    // 异步发送消息
    std::thread([target_network, message]() {
        if (target_network->message_handler_) {
            target_network->message_handler_(message);
        }
    }).detach();
    
    return true;
}

void SimpleRaftNetwork::set_message_handler(std::function<void(const RaftMessage&)> handler) {
    message_handler_ = handler;
}

bool SimpleRaftNetwork::start(const std::string& address, uint16_t port) {
    running_ = true;
    std::cout << "Network: Node " << node_id_ << " started on " << address << ":" << port << std::endl;
    return true;
}

void SimpleRaftNetwork::stop() {
    running_ = false;
    std::cout << "Network: Node " << node_id_ << " stopped" << std::endl;
}

bool SimpleRaftNetwork::add_peer(const std::string& node_id, const std::string& address, uint16_t port) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_[node_id] = std::make_pair(address, port);
    std::cout << "Network: Added peer " << node_id << " at " << address << ":" << port << std::endl;
    return true;
}

bool SimpleRaftNetwork::remove_peer(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = peers_.find(node_id);
    if (it != peers_.end()) {
        peers_.erase(it);
        std::cout << "Network: Removed peer " << node_id << std::endl;
        return true;
    }
    return false;
}

bool SimpleRaftNetwork::is_peer_reachable(const std::string& node_id) {
    // 简单实现：检查是否在全局注册表中
    return get_network(node_id) != nullptr;
}

std::vector<std::string> SimpleRaftNetwork::get_reachable_peers() {
    std::vector<std::string> reachable;
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    for (const auto& pair : peers_) {
        if (is_peer_reachable(pair.first)) {
            reachable.push_back(pair.first);
        }
    }
    
    return reachable;
}

void SimpleRaftNetwork::register_network(const std::string& node_id, SimpleRaftNetwork* network) {
    std::lock_guard<std::mutex> lock(global_networks_mutex_);
    global_networks_[node_id] = network;
}

void SimpleRaftNetwork::unregister_network(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(global_networks_mutex_);
    global_networks_.erase(node_id);
}

SimpleRaftNetwork* SimpleRaftNetwork::get_network(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(global_networks_mutex_);
    auto it = global_networks_.find(node_id);
    return (it != global_networks_.end()) ? it->second : nullptr;
}

bool SimpleRaftNetwork::should_drop_message() const {
    // 模拟1%的丢包率
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dis(0.0, 1.0);
    
    return dis(gen) < 0.01; // 1% 丢包率
}

void SimpleRaftNetwork::simulate_network_delay() const {
    // 模拟1-10ms的网络延迟
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(1, 10);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
}