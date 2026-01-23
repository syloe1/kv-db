#include "load_balancer.h"
#include <algorithm>
#include <numeric>
#include <random>

namespace kvdb {
namespace distributed {

LoadBalancer::LoadBalancer(LoadBalanceStrategy strategy) : strategy_(strategy) {}

LoadBalancer::~LoadBalancer() = default;

bool LoadBalancer::add_node(const NodeInfo& node) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (nodes_.find(node.node_id) != nodes_.end()) {
        return false;  // 节点已存在
    }
    
    nodes_[node.node_id] = node;
    return true;
}

bool LoadBalancer::remove_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    
    nodes_.erase(it);
    return true;
}

bool LoadBalancer::update_node_load(const std::string& node_id, double load_factor) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    
    it->second.load_factor = load_factor;
    it->second.last_heartbeat = std::chrono::steady_clock::now();
    return true;
}

std::vector<NodeInfo> LoadBalancer::get_all_nodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NodeInfo> result;
    
    for (const auto& [node_id, node] : nodes_) {
        result.push_back(node);
    }
    
    return result;
}

std::string LoadBalancer::select_node() const {
    switch (strategy_) {
        case LoadBalanceStrategy::ROUND_ROBIN:
            return round_robin_select();
        case LoadBalanceStrategy::LEAST_CONNECTIONS:
            return least_connections_select();
        case LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN:
            return weighted_round_robin_select();
        case LoadBalanceStrategy::CONSISTENT_HASH:
            // 对于没有key的情况，使用轮询
            return round_robin_select();
        default:
            return round_robin_select();
    }
}

std::string LoadBalancer::select_node_for_key(const std::string& key) const {
    if (strategy_ == LoadBalanceStrategy::CONSISTENT_HASH) {
        return consistent_hash_select(key);
    }
    return select_node();
}

std::vector<std::string> LoadBalancer::select_replica_nodes(int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> healthy_nodes = get_healthy_nodes();
    std::vector<std::string> result;
    
    if (healthy_nodes.empty()) {
        return result;
    }
    
    // 随机选择副本节点
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(healthy_nodes.begin(), healthy_nodes.end(), gen);
    
    int actual_count = std::min(count, static_cast<int>(healthy_nodes.size()));
    for (int i = 0; i < actual_count; ++i) {
        result.push_back(healthy_nodes[i]);
    }
    
    return result;
}

bool LoadBalancer::is_node_healthy(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    
    const auto& node = it->second;
    if (!node.is_alive) {
        return false;
    }
    
    // 检查心跳超时（30秒）
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - node.last_heartbeat);
    return elapsed.count() < 30;
}

void LoadBalancer::mark_node_unhealthy(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.is_alive = false;
    }
}

void LoadBalancer::mark_node_healthy(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.is_alive = true;
        it->second.last_heartbeat = std::chrono::steady_clock::now();
    }
}

void LoadBalancer::set_strategy(LoadBalanceStrategy strategy) {
    std::lock_guard<std::mutex> lock(mutex_);
    strategy_ = strategy;
}

LoadBalanceStrategy LoadBalancer::get_strategy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return strategy_;
}

size_t LoadBalancer::get_node_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}

size_t LoadBalancer::get_healthy_node_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return get_healthy_nodes().size();
}

double LoadBalancer::get_average_load() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (nodes_.empty()) {
        return 0.0;
    }
    
    double total_load = 0.0;
    for (const auto& [node_id, node] : nodes_) {
        if (node.is_alive) {
            total_load += node.load_factor;
        }
    }
    
    return total_load / nodes_.size();
}

std::string LoadBalancer::round_robin_select() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> healthy_nodes = get_healthy_nodes();
    
    if (healthy_nodes.empty()) {
        return "";
    }
    
    size_t index = round_robin_index_.fetch_add(1) % healthy_nodes.size();
    return healthy_nodes[index];
}

std::string LoadBalancer::least_connections_select() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> healthy_nodes = get_healthy_nodes();
    
    if (healthy_nodes.empty()) {
        return "";
    }
    
    // 选择负载最低的节点
    std::string best_node = healthy_nodes[0];
    double min_load = nodes_.at(best_node).load_factor;
    
    for (const auto& node_id : healthy_nodes) {
        double load = nodes_.at(node_id).load_factor;
        if (load < min_load) {
            min_load = load;
            best_node = node_id;
        }
    }
    
    return best_node;
}

std::string LoadBalancer::weighted_round_robin_select() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> healthy_nodes = get_healthy_nodes();
    
    if (healthy_nodes.empty()) {
        return "";
    }
    
    // 基于负载因子的加权轮询
    std::vector<double> weights;
    double total_weight = 0.0;
    
    for (const auto& node_id : healthy_nodes) {
        double weight = 1.0 / (1.0 + nodes_.at(node_id).load_factor);  // 负载越低权重越高
        weights.push_back(weight);
        total_weight += weight;
    }
    
    // 随机选择
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, total_weight);
    double random_value = dis(gen);
    
    double cumulative_weight = 0.0;
    for (size_t i = 0; i < healthy_nodes.size(); ++i) {
        cumulative_weight += weights[i];
        if (random_value <= cumulative_weight) {
            return healthy_nodes[i];
        }
    }
    
    return healthy_nodes.back();
}

std::string LoadBalancer::consistent_hash_select(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> healthy_nodes = get_healthy_nodes();
    
    if (healthy_nodes.empty()) {
        return "";
    }
    
    // 一致性哈希选择
    uint64_t key_hash = hash_key(key);
    std::string best_node = healthy_nodes[0];
    uint64_t min_distance = UINT64_MAX;
    
    for (const auto& node_id : healthy_nodes) {
        uint64_t node_hash = hash_key(node_id);
        uint64_t distance = (node_hash >= key_hash) ? 
                           (node_hash - key_hash) : 
                           (UINT64_MAX - key_hash + node_hash);
        
        if (distance < min_distance) {
            min_distance = distance;
            best_node = node_id;
        }
    }
    
    return best_node;
}

std::vector<std::string> LoadBalancer::get_healthy_nodes() const {
    std::vector<std::string> result;
    
    for (const auto& [node_id, node] : nodes_) {
        if (node.is_alive) {
            // 检查心跳超时（30秒）
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - node.last_heartbeat);
            if (elapsed.count() < 30) {
                result.push_back(node_id);
            }
        }
    }
    
    return result;
}

uint64_t LoadBalancer::hash_key(const std::string& key) const {
    uint64_t hash = 0;
    for (char c : key) {
        hash = hash * 31 + c;
    }
    return hash;
}

} // namespace distributed
} // namespace kvdb