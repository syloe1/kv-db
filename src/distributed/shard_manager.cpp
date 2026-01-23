#include "shard_manager.h"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace kvdb {
namespace distributed {

ShardManager::ShardManager() {
    // 创建默认分片
    create_shard("shard_0", "", "~");
}

ShardManager::~ShardManager() = default;

bool ShardManager::create_shard(const std::string& shard_id, const std::string& start_key, 
                                const std::string& end_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (shards_.find(shard_id) != shards_.end()) {
        return false;  // 分片已存在
    }
    
    auto shard = std::make_unique<ShardInfo>(shard_id, start_key, end_key);
    shards_[shard_id] = std::move(shard);
    
    update_hash_ring();
    return true;
}

bool ShardManager::delete_shard(const std::string& shard_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shards_.find(shard_id);
    if (it == shards_.end()) {
        return false;
    }
    
    shards_.erase(it);
    update_hash_ring();
    return true;
}

std::string ShardManager::get_shard_for_key(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& [shard_id, shard] : shards_) {
        if (is_key_in_range(key, shard->start_key, shard->end_key)) {
            return shard_id;
        }
    }
    
    return "";  // 未找到合适的分片
}

std::vector<std::string> ShardManager::get_shards_for_range(const std::string& start_key, 
                                                           const std::string& end_key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    
    for (const auto& [shard_id, shard] : shards_) {
        // 检查范围是否有重叠
        if (!(end_key < shard->start_key || start_key > shard->end_key)) {
            result.push_back(shard_id);
        }
    }
    
    return result;
}

bool ShardManager::add_replica(const std::string& shard_id, const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shards_.find(shard_id);
    if (it == shards_.end()) {
        return false;
    }
    
    auto& replicas = it->second->replica_nodes;
    if (std::find(replicas.begin(), replicas.end(), node_id) == replicas.end()) {
        replicas.push_back(node_id);
    }
    
    return true;
}

bool ShardManager::remove_replica(const std::string& shard_id, const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shards_.find(shard_id);
    if (it == shards_.end()) {
        return false;
    }
    
    auto& replicas = it->second->replica_nodes;
    replicas.erase(std::remove(replicas.begin(), replicas.end(), node_id), replicas.end());
    
    return true;
}

bool ShardManager::set_primary_node(const std::string& shard_id, const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shards_.find(shard_id);
    if (it == shards_.end()) {
        return false;
    }
    
    it->second->primary_node = node_id;
    return true;
}

std::vector<std::string> ShardManager::get_replica_nodes(const std::string& shard_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shards_.find(shard_id);
    if (it == shards_.end()) {
        return {};
    }
    
    return it->second->replica_nodes;
}

std::string ShardManager::get_primary_node(const std::string& shard_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shards_.find(shard_id);
    if (it == shards_.end()) {
        return "";
    }
    
    return it->second->primary_node;
}

bool ShardManager::rebalance_shards() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 简单的重平衡策略：基于分片大小
    std::vector<std::pair<std::string, size_t>> shard_sizes;
    
    for (const auto& [shard_id, shard] : shards_) {
        // 这里应该获取实际的分片大小，暂时使用模拟值
        size_t size = hash_key(shard_id) % 1000;
        shard_sizes.emplace_back(shard_id, size);
    }
    
    // 按大小排序
    std::sort(shard_sizes.begin(), shard_sizes.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // 分割过大的分片
    for (const auto& [shard_id, size] : shard_sizes) {
        if (size > 500) {  // 阈值
            std::string split_key = shard_id + "_split";
            split_shard(shard_id, split_key);
        }
    }
    
    return true;
}

bool ShardManager::migrate_shard(const std::string& shard_id, const std::string& from_node, 
                                const std::string& to_node) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shards_.find(shard_id);
    if (it == shards_.end()) {
        return false;
    }
    
    auto& shard = it->second;
    
    // 更新主节点
    if (shard->primary_node == from_node) {
        shard->primary_node = to_node;
    }
    
    // 更新副本节点
    auto& replicas = shard->replica_nodes;
    for (auto& replica : replicas) {
        if (replica == from_node) {
            replica = to_node;
            break;
        }
    }
    
    std::cout << "Migrated shard " << shard_id << " from " << from_node 
              << " to " << to_node << std::endl;
    
    return true;
}

std::string ShardManager::get_node_by_hash(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (hash_ring_.empty()) {
        return "";
    }
    
    uint64_t key_hash = hash_key(key);
    
    // 在哈希环中查找第一个大于等于key_hash的节点
    auto it = std::lower_bound(hash_ring_.begin(), hash_ring_.end(), 
                              std::make_pair(key_hash, std::string("")),
                              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    if (it == hash_ring_.end()) {
        return hash_ring_[0].second;  // 环形，返回第一个节点
    }
    
    return it->second;
}

void ShardManager::update_hash_ring() {
    hash_ring_.clear();
    
    // 为每个分片的每个副本节点在哈希环上创建虚拟节点
    for (const auto& [shard_id, shard] : shards_) {
        for (const auto& node : shard->replica_nodes) {
            for (int i = 0; i < 100; ++i) {  // 每个节点100个虚拟节点
                std::string virtual_node = node + "_" + std::to_string(i);
                uint64_t hash = hash_key(virtual_node);
                hash_ring_.emplace_back(hash, node);
            }
        }
    }
    
    // 排序哈希环
    std::sort(hash_ring_.begin(), hash_ring_.end());
}

std::vector<ShardInfo> ShardManager::get_all_shards() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ShardInfo> result;
    
    for (const auto& [shard_id, shard] : shards_) {
        result.push_back(*shard);
    }
    
    return result;
}

ShardInfo* ShardManager::get_shard_info(const std::string& shard_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shards_.find(shard_id);
    if (it == shards_.end()) {
        return nullptr;
    }
    
    return it->second.get();
}

uint64_t ShardManager::hash_key(const std::string& key) const {
    // 简单的哈希函数
    uint64_t hash = 0;
    for (char c : key) {
        hash = hash * 31 + c;
    }
    return hash;
}

bool ShardManager::is_key_in_range(const std::string& key, const std::string& start, 
                                  const std::string& end) const {
    if (start.empty() && end == "~") {
        return true;  // 全范围
    }
    
    return (start.empty() || key >= start) && (end == "~" || key < end);
}

bool ShardManager::split_shard(const std::string& shard_id, const std::string& split_key) {
    auto it = shards_.find(shard_id);
    if (it == shards_.end()) {
        return false;
    }
    
    auto& original_shard = it->second;
    std::string new_shard_id = shard_id + "_split";
    
    // 创建新分片
    auto new_shard = std::make_unique<ShardInfo>(new_shard_id, split_key, original_shard->end_key);
    new_shard->replica_nodes = original_shard->replica_nodes;
    new_shard->primary_node = original_shard->primary_node;
    
    // 更新原分片
    original_shard->end_key = split_key;
    
    shards_[new_shard_id] = std::move(new_shard);
    
    std::cout << "Split shard " << shard_id << " at key " << split_key << std::endl;
    return true;
}

bool ShardManager::merge_shards(const std::string& shard1_id, const std::string& shard2_id) {
    auto it1 = shards_.find(shard1_id);
    auto it2 = shards_.find(shard2_id);
    
    if (it1 == shards_.end() || it2 == shards_.end()) {
        return false;
    }
    
    // 合并逻辑（简化实现）
    auto& shard1 = it1->second;
    auto& shard2 = it2->second;
    
    // 扩展第一个分片的范围
    if (shard2->end_key > shard1->end_key) {
        shard1->end_key = shard2->end_key;
    }
    
    // 删除第二个分片
    shards_.erase(it2);
    
    std::cout << "Merged shards " << shard1_id << " and " << shard2_id << std::endl;
    return true;
}

} // namespace distributed
} // namespace kvdb