#include "partition_recovery_types.h"
#include <algorithm>

// PartitionRecoveryContext 实现
PartitionRecoveryContext::PartitionRecoveryContext(const std::string& recovery_id)
    : recovery_id_(recovery_id), state_(PartitionState::NORMAL),
      start_time_(std::chrono::system_clock::now()),
      detection_count_(0), recovery_count_(0) {
}

PartitionRecoveryContext::~PartitionRecoveryContext() {
}

PartitionState PartitionRecoveryContext::get_state() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return state_;
}

void PartitionRecoveryContext::set_state(PartitionState state) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    state_ = state;
}

void PartitionRecoveryContext::add_partition(const PartitionInfo& partition) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    // 检查是否已存在
    auto it = std::find_if(partitions_.begin(), partitions_.end(),
                          [&partition](const PartitionInfo& p) {
                              return p.partition_id == partition.partition_id;
                          });
    
    if (it == partitions_.end()) {
        partitions_.push_back(partition);
    } else {
        // 更新现有分区信息
        *it = partition;
    }
}

void PartitionRecoveryContext::update_partition_state(const std::string& partition_id, PartitionState state) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    auto it = std::find_if(partitions_.begin(), partitions_.end(),
                          [&partition_id](PartitionInfo& p) {
                              return p.partition_id == partition_id;
                          });
    
    if (it != partitions_.end()) {
        it->state = state;
        if (state == PartitionState::RECOVERED) {
            it->recovery_time = std::chrono::system_clock::now();
        }
    }
}

std::vector<PartitionInfo> PartitionRecoveryContext::get_partitions() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return partitions_;
}

PartitionInfo* PartitionRecoveryContext::get_partition(const std::string& partition_id) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    auto it = std::find_if(partitions_.begin(), partitions_.end(),
                          [&partition_id](const PartitionInfo& p) {
                              return p.partition_id == partition_id;
                          });
    
    return (it != partitions_.end()) ? &(*it) : nullptr;
}

void PartitionRecoveryContext::add_node(const NodeInfo& node) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    // 检查是否已存在
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
                          [&node](const NodeInfo& n) {
                              return n.node_id == node.node_id;
                          });
    
    if (it == nodes_.end()) {
        nodes_.push_back(node);
    } else {
        // 更新现有节点信息
        *it = node;
    }
}

void PartitionRecoveryContext::update_node_health(const std::string& node_id, NodeHealthState state) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
                          [&node_id](NodeInfo& n) {
                              return n.node_id == node_id;
                          });
    
    if (it != nodes_.end()) {
        it->health_state = state;
        it->last_response = std::chrono::system_clock::now();
    }
}

std::vector<NodeInfo> PartitionRecoveryContext::get_nodes() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return nodes_;
}

NodeInfo* PartitionRecoveryContext::get_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
                          [&node_id](const NodeInfo& n) {
                              return n.node_id == node_id;
                          });
    
    return (it != nodes_.end()) ? &(*it) : nullptr;
}

void PartitionRecoveryContext::add_conflict(const ConsistencyConflict& conflict) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    // 检查是否已存在相同键的冲突
    auto it = std::find_if(conflicts_.begin(), conflicts_.end(),
                          [&conflict](const ConsistencyConflict& c) {
                              return c.key == conflict.key;
                          });
    
    if (it == conflicts_.end()) {
        conflicts_.push_back(conflict);
    } else {
        // 合并冲突信息
        for (const auto& value : conflict.conflicting_values) {
            if (std::find(it->conflicting_values.begin(), it->conflicting_values.end(), value) 
                == it->conflicting_values.end()) {
                it->conflicting_values.push_back(value);
            }
        }
        
        for (const auto& node : conflict.nodes) {
            if (std::find(it->nodes.begin(), it->nodes.end(), node) == it->nodes.end()) {
                it->nodes.push_back(node);
            }
        }
        
        // 合并时间戳和版本
        it->timestamps.insert(it->timestamps.end(), 
                             conflict.timestamps.begin(), conflict.timestamps.end());
        it->versions.insert(it->versions.end(), 
                           conflict.versions.begin(), conflict.versions.end());
    }
}

void PartitionRecoveryContext::resolve_conflict(const std::string& key, const std::string& resolved_value) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    auto it = std::find_if(conflicts_.begin(), conflicts_.end(),
                          [&key](ConsistencyConflict& c) {
                              return c.key == key;
                          });
    
    if (it != conflicts_.end()) {
        it->resolved_value = resolved_value;
        it->is_resolved = true;
    }
}

std::vector<ConsistencyConflict> PartitionRecoveryContext::get_unresolved_conflicts() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    std::vector<ConsistencyConflict> unresolved;
    for (const auto& conflict : conflicts_) {
        if (!conflict.is_resolved) {
            unresolved.push_back(conflict);
        }
    }
    
    return unresolved;
}

bool PartitionRecoveryContext::has_unresolved_conflicts() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    return std::any_of(conflicts_.begin(), conflicts_.end(),
                      [](const ConsistencyConflict& c) {
                          return !c.is_resolved;
                      });
}