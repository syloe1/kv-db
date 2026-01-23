#include "simple_distributed_network.h"
#include <iostream>
#include <thread>
#include <random>

// 静态成员初始化
std::mutex SimpleDistributedTransactionNetwork::global_networks_mutex_;
std::unordered_map<std::string, SimpleDistributedTransactionNetwork*> 
    SimpleDistributedTransactionNetwork::global_networks_;

SimpleDistributedTransactionNetwork::SimpleDistributedTransactionNetwork(const std::string& node_id)
    : node_id_(node_id), running_(false) {
    register_network(node_id_, this);
}

SimpleDistributedTransactionNetwork::~SimpleDistributedTransactionNetwork() {
    stop();
    unregister_network(node_id_);
}

bool SimpleDistributedTransactionNetwork::send_message(
    const std::string& target_node, const TwoPhaseCommitMessage& message) {
    
    if (!running_) {
        return false;
    }
    
    // 模拟网络延迟和丢包
    if (should_drop_message()) {
        std::cout << "DistributedTxnNetwork: Message from " << node_id_ 
                  << " to " << target_node << " dropped" << std::endl;
        return false;
    }
    
    simulate_network_delay();
    
    // 查找目标节点的网络实例
    SimpleDistributedTransactionNetwork* target_network = get_network(target_node);
    if (!target_network) {
        std::cout << "DistributedTxnNetwork: Target node " << target_node << " not found" << std::endl;
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

bool SimpleDistributedTransactionNetwork::broadcast_message(
    const std::vector<std::string>& target_nodes,
    const TwoPhaseCommitMessage& message) {
    
    bool all_success = true;
    for (const auto& node : target_nodes) {
        if (!send_message(node, message)) {
            all_success = false;
        }
    }
    
    return all_success;
}

void SimpleDistributedTransactionNetwork::set_message_handler(
    std::function<void(const TwoPhaseCommitMessage&)> handler) {
    message_handler_ = handler;
}

bool SimpleDistributedTransactionNetwork::start(const std::string& address, uint16_t port) {
    running_ = true;
    std::cout << "DistributedTxnNetwork: Node " << node_id_ 
              << " started on " << address << ":" << port << std::endl;
    return true;
}

void SimpleDistributedTransactionNetwork::stop() {
    running_ = false;
    std::cout << "DistributedTxnNetwork: Node " << node_id_ << " stopped" << std::endl;
}

bool SimpleDistributedTransactionNetwork::add_node(
    const std::string& node_id, const std::string& address, uint16_t port) {
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_[node_id] = std::make_pair(address, port);
    std::cout << "DistributedTxnNetwork: Added peer " << node_id 
              << " at " << address << ":" << port << std::endl;
    return true;
}

bool SimpleDistributedTransactionNetwork::remove_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = peers_.find(node_id);
    if (it != peers_.end()) {
        peers_.erase(it);
        std::cout << "DistributedTxnNetwork: Removed peer " << node_id << std::endl;
        return true;
    }
    return false;
}

bool SimpleDistributedTransactionNetwork::is_node_reachable(const std::string& node_id) {
    return get_network(node_id) != nullptr;
}

std::vector<std::string> SimpleDistributedTransactionNetwork::get_reachable_nodes() {
    std::vector<std::string> reachable;
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    for (const auto& pair : peers_) {
        if (is_node_reachable(pair.first)) {
            reachable.push_back(pair.first);
        }
    }
    
    return reachable;
}

void SimpleDistributedTransactionNetwork::register_network(
    const std::string& node_id, SimpleDistributedTransactionNetwork* network) {
    
    std::lock_guard<std::mutex> lock(global_networks_mutex_);
    global_networks_[node_id] = network;
}

void SimpleDistributedTransactionNetwork::unregister_network(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(global_networks_mutex_);
    global_networks_.erase(node_id);
}

SimpleDistributedTransactionNetwork* SimpleDistributedTransactionNetwork::get_network(
    const std::string& node_id) {
    
    std::lock_guard<std::mutex> lock(global_networks_mutex_);
    auto it = global_networks_.find(node_id);
    return (it != global_networks_.end()) ? it->second : nullptr;
}

bool SimpleDistributedTransactionNetwork::should_drop_message() const {
    // 模拟0.5%的丢包率
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dis(0.0, 1.0);
    
    return dis(gen) < 0.005; // 0.5% 丢包率
}

void SimpleDistributedTransactionNetwork::simulate_network_delay() const {
    // 模拟1-5ms的网络延迟
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(1, 5);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
}