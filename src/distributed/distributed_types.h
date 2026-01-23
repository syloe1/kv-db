#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>

namespace kvdb {
namespace distributed {

// 节点信息
struct NodeInfo {
    std::string node_id;
    std::string host;
    int port;
    bool is_alive;
    std::chrono::steady_clock::time_point last_heartbeat;
    double load_factor;  // 负载因子
    
    NodeInfo() : node_id(""), host(""), port(0), is_alive(false), 
                 last_heartbeat(std::chrono::steady_clock::now()), load_factor(0.0) {}
    
    NodeInfo(const std::string& id, const std::string& h, int p)
        : node_id(id), host(h), port(p), is_alive(true), 
          last_heartbeat(std::chrono::steady_clock::now()), load_factor(0.0) {}
};

// 分片信息
struct ShardInfo {
    std::string shard_id;
    std::string start_key;
    std::string end_key;
    std::vector<std::string> replica_nodes;  // 副本节点列表
    std::string primary_node;                // 主节点
    
    ShardInfo(const std::string& id, const std::string& start, const std::string& end)
        : shard_id(id), start_key(start), end_key(end) {}
};

// 分布式操作类型
enum class DistributedOpType {
    GET,
    PUT,
    DELETE,
    SCAN
};

// 分布式请求
struct DistributedRequest {
    DistributedOpType op_type;
    std::string key;
    std::string value;
    std::string start_key;  // for scan
    std::string end_key;    // for scan
    int limit;              // for scan
    
    DistributedRequest(DistributedOpType type, const std::string& k, const std::string& v = "")
        : op_type(type), key(k), value(v), limit(100) {}
};

// 分布式响应
struct DistributedResponse {
    bool success;
    std::string value;
    std::vector<std::pair<std::string, std::string>> results;  // for scan
    std::string error_message;
    
    DistributedResponse() : success(false) {}
};

// 负载均衡策略
enum class LoadBalanceStrategy {
    ROUND_ROBIN,
    LEAST_CONNECTIONS,
    WEIGHTED_ROUND_ROBIN,
    CONSISTENT_HASH
};

// 故障转移状态
enum class FailoverState {
    NORMAL,
    DETECTING,
    RECOVERING,
    COMPLETED
};

} // namespace distributed
} // namespace kvdb