#pragma once

#include "distributed_types.h"
#include <mutex>
#include <functional>

namespace kvdb {
namespace distributed {

class ShardManager {
public:
    ShardManager();
    ~ShardManager();
    
    // 分片管理
    bool create_shard(const std::string& shard_id, const std::string& start_key, 
                     const std::string& end_key);
    bool delete_shard(const std::string& shard_id);
    std::string get_shard_for_key(const std::string& key) const;
    std::vector<std::string> get_shards_for_range(const std::string& start_key, 
                                                  const std::string& end_key) const;
    
    // 副本管理
    bool add_replica(const std::string& shard_id, const std::string& node_id);
    bool remove_replica(const std::string& shard_id, const std::string& node_id);
    bool set_primary_node(const std::string& shard_id, const std::string& node_id);
    std::vector<std::string> get_replica_nodes(const std::string& shard_id) const;
    std::string get_primary_node(const std::string& shard_id) const;
    
    // 分片重平衡
    bool rebalance_shards();
    bool migrate_shard(const std::string& shard_id, const std::string& from_node, 
                      const std::string& to_node);
    
    // 一致性哈希
    std::string get_node_by_hash(const std::string& key) const;
    void update_hash_ring();
    
    // 获取分片信息
    std::vector<ShardInfo> get_all_shards() const;
    ShardInfo* get_shard_info(const std::string& shard_id);
    
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<ShardInfo>> shards_;
    std::vector<std::pair<uint64_t, std::string>> hash_ring_;  // 一致性哈希环
    
    // 哈希函数
    uint64_t hash_key(const std::string& key) const;
    bool is_key_in_range(const std::string& key, const std::string& start, 
                        const std::string& end) const;
    
    // 分片分割
    bool split_shard(const std::string& shard_id, const std::string& split_key);
    bool merge_shards(const std::string& shard1_id, const std::string& shard2_id);
};

} // namespace distributed
} // namespace kvdb