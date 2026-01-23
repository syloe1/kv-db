#include "simple_partition_network.h"
#include <iostream>
#include <thread>
#include <random>

// 静态成员初始化
std::mutex SimplePartitionRecoveryNetwork::global_networks_mutex_;
std::unordered_map<std::string, SimplePartitionRecoveryNetwork*> 
    SimplePartitionRecoveryNetwork::global_networks_;

SimplePartitionRecoveryNetwork::SimplePartitionRecoveryNetwork(const std::string& node_id)
    : node_id_(node_id), running_(false), is_partitioned_(false), network_failure_rate_(0.01) {
    register_network(node_id_, this);
}

SimplePartitionRecoveryNetwork::~SimplePartitionRecoveryNetwork() {
    stop();
    unregister_network(node_id_);
}

bool SimplePartitionRecoveryNetwork::send_message(
    const std::string& target_node, const PartitionDetectionMessage& message) {
    
    if (!running_) {
        return false;
    }
    
    // 检查网络分区
    if (!can_reach_node(target_node)) {
        std::cout << "PartitionNetwork: Cannot reach " << target_node 
                  << " from " << node_id_ << " (partitioned)" << std::endl;
        return false;
    }
    
    // 模拟网络延迟和丢包
    if (should_drop_message()) {
        std::cout << "PartitionNetwork: Message from " << node_id_ 
                  << " to " << target_node << " dropped" << std::endl;
        return false;
    }
    
    simulate_network_delay();
    
    // 查找目标节点的网络实例
    SimplePartitionRecoveryNetwork* target_network = get_network(target_node);
    if (!target_network) {
        std::cout << "PartitionNetwork: Target node " << target_node << " not found" << std::endl;
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

bool SimplePartitionRecoveryNetwork::broadcast_message(
    const std::vector<std::string>& target_nodes,
    const PartitionDetectionMessage& message) {
    
    bool all_success = true;
    for (const auto& node : target_nodes) {
        if (!send_message(node, message)) {
            all_success = false;
        }
    }
    
    return all_success;
}

void SimplePartitionRecoveryNetwork::set_message_handler(
    std::function<void(const PartitionDetectionMessage&)> handler) {
    message_handler_ = handler;
}

bool SimplePartitionRecoveryNetwork::start(const std::string& address, uint16_t port) {
    running_ = true;
    std::cout << "PartitionNetwork: Node " << node_id_ 
              << " started on " << address << ":" << port << std::endl;
    return true;
}

void SimplePartitionRecoveryNetwork::stop() {
    running_ = false;
    std::cout << "PartitionNetwork: Node " << node_id_ << " stopped" << std::endl;
}

bool SimplePartitionRecoveryNetwork::add_node(
    const std::string& node_id, const std::string& address, uint16_t port) {
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_[node_id] = std::make_pair(address, port);
    
    // 默认情况下所有节点都是可达的
    {
        std::lock_guard<std::mutex> partition_lock(partition_mutex_);
        if (!is_partitioned_) {
            reachable_nodes_.insert(node_id);
        }
    }
    
    std::cout << "PartitionNetwork: Added peer " << node_id 
              << " at " << address << ":" << port << std::endl;
    return true;
}

bool SimplePartitionRecoveryNetwork::remove_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = peers_.find(node_id);
    if (it != peers_.end()) {
        peers_.erase(it);
        
        // 从可达节点中移除
        {
            std::lock_guard<std::mutex> partition_lock(partition_mutex_);
            reachable_nodes_.erase(node_id);
        }
        
        std::cout << "PartitionNetwork: Removed peer " << node_id << std::endl;
        return true;
    }
    return false;
}

bool SimplePartitionRecoveryNetwork::is_node_reachable(const std::string& node_id) {
    return can_reach_node(node_id) && get_network(node_id) != nullptr;
}

std::vector<std::string> SimplePartitionRecoveryNetwork::get_reachable_nodes() {
    std::vector<std::string> reachable;
    
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (const auto& pair : peers_) {
            if (is_node_reachable(pair.first)) {
                reachable.push_back(pair.first);
            }
        }
    }
    
    return reachable;
}

double SimplePartitionRecoveryNetwork::ping_node(const std::string& node_id) {
    if (!can_reach_node(node_id)) {
        return -1.0; // 不可达
    }
    
    // 模拟ping延迟
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dis(1.0, 50.0);
    
    return dis(gen); // 1-50ms的延迟
}

bool SimplePartitionRecoveryNetwork::test_connectivity(const std::string& node_id) {
    return is_node_reachable(node_id);
}

void SimplePartitionRecoveryNetwork::simulate_partition(
    const std::vector<std::string>& partition1,
    const std::vector<std::string>& partition2) {
    
    std::lock_guard<std::mutex> lock(partition_mutex_);
    
    is_partitioned_ = true;
    reachable_nodes_.clear();
    
    // 确定当前节点属于哪个分区
    bool in_partition1 = std::find(partition1.begin(), partition1.end(), node_id_) != partition1.end();
    bool in_partition2 = std::find(partition2.begin(), partition2.end(), node_id_) != partition2.end();
    
    if (in_partition1) {
        for (const auto& node : partition1) {
            reachable_nodes_.insert(node);
        }
        std::cout << "PartitionNetwork: Node " << node_id_ 
                  << " is in partition 1 with " << partition1.size() << " nodes" << std::endl;
    } else if (in_partition2) {
        for (const auto& node : partition2) {
            reachable_nodes_.insert(node);
        }
        std::cout << "PartitionNetwork: Node " << node_id_ 
                  << " is in partition 2 with " << partition2.size() << " nodes" << std::endl;
    } else {
        // 节点被完全隔离
        reachable_nodes_.insert(node_id_); // 只能到达自己
        std::cout << "PartitionNetwork: Node " << node_id_ << " is isolated" << std::endl;
    }
}

void SimplePartitionRecoveryNetwork::heal_partition() {
    std::lock_guard<std::mutex> lock(partition_mutex_);
    
    is_partitioned_ = false;
    reachable_nodes_.clear();
    
    // 恢复后所有节点都可达
    {
        std::lock_guard<std::mutex> peers_lock(peers_mutex_);
        for (const auto& pair : peers_) {
            reachable_nodes_.insert(pair.first);
        }
    }
    
    std::cout << "PartitionNetwork: Network partition healed for node " << node_id_ << std::endl;
}

void SimplePartitionRecoveryNetwork::set_network_failure_rate(double failure_rate) {
    network_failure_rate_ = std::max(0.0, std::min(1.0, failure_rate));
    std::cout << "PartitionNetwork: Set failure rate to " << (failure_rate * 100) << "%" << std::endl;
}

void SimplePartitionRecoveryNetwork::register_network(
    const std::string& node_id, SimplePartitionRecoveryNetwork* network) {
    
    std::lock_guard<std::mutex> lock(global_networks_mutex_);
    global_networks_[node_id] = network;
}

void SimplePartitionRecoveryNetwork::unregister_network(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(global_networks_mutex_);
    global_networks_.erase(node_id);
}

SimplePartitionRecoveryNetwork* SimplePartitionRecoveryNetwork::get_network(
    const std::string& node_id) {
    
    std::lock_guard<std::mutex> lock(global_networks_mutex_);
    auto it = global_networks_.find(node_id);
    return (it != global_networks_.end()) ? it->second : nullptr;
}

bool SimplePartitionRecoveryNetwork::should_drop_message() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dis(0.0, 1.0);
    
    return dis(gen) < network_failure_rate_;
}

void SimplePartitionRecoveryNetwork::simulate_network_delay() const {
    // 模拟1-10ms的网络延迟
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(1, 10);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
}

bool SimplePartitionRecoveryNetwork::can_reach_node(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(partition_mutex_);
    
    if (!is_partitioned_) {
        return true; // 没有分区时所有节点都可达
    }
    
    return reachable_nodes_.find(node_id) != reachable_nodes_.end();
}