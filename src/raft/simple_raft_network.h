#pragma once

#include "raft_node.h"
#include <unordered_map>
#include <functional>
#include <thread>
#include <mutex>

// 简单的内存网络实现，用于测试
class SimpleRaftNetwork : public RaftNetworkInterface {
public:
    SimpleRaftNetwork(const std::string& node_id);
    ~SimpleRaftNetwork() override;
    
    // 发送消息
    bool send_message(const std::string& to, const RaftMessage& message) override;
    
    // 设置消息处理回调
    void set_message_handler(std::function<void(const RaftMessage&)> handler) override;
    
    // 启动和停止网络服务
    bool start(const std::string& address, uint16_t port) override;
    void stop() override;
    
    // 节点管理
    bool add_peer(const std::string& node_id, const std::string& address, uint16_t port) override;
    bool remove_peer(const std::string& node_id) override;
    
    // 网络状态
    bool is_peer_reachable(const std::string& node_id) override;
    std::vector<std::string> get_reachable_peers() override;
    
    // 静态方法用于节点间通信
    static void register_network(const std::string& node_id, SimpleRaftNetwork* network);
    static void unregister_network(const std::string& node_id);
    static SimpleRaftNetwork* get_network(const std::string& node_id);

private:
    std::string node_id_;
    std::function<void(const RaftMessage&)> message_handler_;
    bool running_;
    
    // 对等节点信息
    std::mutex peers_mutex_;
    std::unordered_map<std::string, std::pair<std::string, uint16_t>> peers_;
    
    // 全局网络注册表
    static std::mutex global_networks_mutex_;
    static std::unordered_map<std::string, SimpleRaftNetwork*> global_networks_;
    
    // 模拟网络延迟和丢包
    bool should_drop_message() const;
    void simulate_network_delay() const;
};