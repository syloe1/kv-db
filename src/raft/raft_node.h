#pragma once

#include "raft_types.h"
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <functional>
#include <random>
#include <future>

// 前向声明
class RaftNetworkInterface;
class RaftStateMachine;

// Raft节点实现
class RaftNode {
public:
    RaftNode(const RaftConfig& config, 
             std::shared_ptr<RaftNetworkInterface> network,
             std::shared_ptr<RaftStateMachine> state_machine);
    ~RaftNode();
    
    // 节点生命周期
    bool start();
    void stop();
    bool is_running() const;
    
    // 客户端接口
    ClientResponse handle_client_request(const ClientRequest& request);
    bool is_leader() const;
    std::string get_leader_id() const;
    
    // Raft消息处理
    void handle_message(const RaftMessage& message);
    
    // 状态查询
    RaftState get_state() const;
    uint64_t get_current_term() const;
    RaftStats get_statistics() const;
    
    // 配置管理
    bool add_node(const std::string& node_id, const std::string& address, uint16_t port);
    bool remove_node(const std::string& node_id);
    std::vector<RaftNodeInfo> get_cluster_nodes() const;

private:
    // 配置和依赖
    RaftConfig config_;
    std::shared_ptr<RaftNetworkInterface> network_;
    std::shared_ptr<RaftStateMachine> state_machine_;
    
    // Raft状态（持久化状态）
    mutable std::mutex state_mutex_;
    uint64_t current_term_;
    std::string voted_for_;
    std::vector<LogEntry> log_;
    
    // Raft状态（易失性状态）
    RaftState state_;
    uint64_t commit_index_;
    uint64_t last_applied_;
    
    // 领导者状态（仅领导者维护）
    std::unordered_map<std::string, uint64_t> next_index_;   // 发送给每个服务器的下一个日志条目索引
    std::unordered_map<std::string, uint64_t> match_index_;  // 已知的每个服务器上复制的最高日志条目索引
    
    // 集群信息
    mutable std::mutex cluster_mutex_;
    std::unordered_map<std::string, RaftNodeInfo> cluster_nodes_;
    std::string current_leader_;
    
    // 定时器和线程
    std::atomic<bool> running_;
    std::thread main_loop_thread_;
    std::thread heartbeat_thread_;
    
    // 选举相关
    std::chrono::system_clock::time_point last_heartbeat_;
    std::chrono::milliseconds election_timeout_;
    std::unordered_set<std::string> votes_received_;
    
    // 消息队列
    mutable std::mutex message_queue_mutex_;
    std::queue<RaftMessage> message_queue_;
    std::condition_variable message_queue_cv_;
    
    // 客户端请求队列
    mutable std::mutex client_request_mutex_;
    std::queue<std::pair<ClientRequest, std::promise<ClientResponse>>> client_request_queue_;
    std::condition_variable client_request_cv_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    RaftStats stats_;
    
    // 随机数生成器
    mutable std::random_device rd_;
    mutable std::mt19937 gen_;
    
    // 主循环
    void main_loop();
    void heartbeat_loop();
    
    // 状态转换
    void become_follower(uint64_t term);
    void become_candidate();
    void become_leader();
    
    // 选举逻辑
    void start_election();
    void handle_request_vote(const RequestVoteMessage& msg, const std::string& from);
    void handle_request_vote_reply(const RequestVoteReply& msg, const std::string& from);
    
    // 日志复制
    void send_heartbeats();
    void send_append_entries(const std::string& node_id);
    void handle_append_entries(const AppendEntriesMessage& msg, const std::string& from);
    void handle_append_entries_reply(const AppendEntriesReply& msg, const std::string& from);
    
    // 快照处理
    void handle_install_snapshot(const InstallSnapshotMessage& msg, const std::string& from);
    void handle_install_snapshot_reply(const InstallSnapshotReply& msg, const std::string& from);
    
    // 日志管理
    bool append_log_entry(const LogEntry& entry);
    bool append_log_entries(const std::vector<LogEntry>& entries, uint64_t prev_log_index, uint64_t prev_log_term);
    LogEntry get_log_entry(uint64_t index) const;
    uint64_t get_last_log_index() const;
    uint64_t get_last_log_term() const;
    uint64_t get_log_term(uint64_t index) const;
    
    // 提交和应用
    void update_commit_index();
    void apply_committed_entries();
    
    // 客户端请求处理
    void process_client_requests();
    ClientResponse process_client_request(const ClientRequest& request);
    
    // 网络通信
    void send_message(const std::string& to, const RaftMessage& message);
    void broadcast_message(const RaftMessage& message);
    
    // 工具方法
    bool is_election_timeout() const;
    void reset_election_timeout();
    void update_statistics();
    bool is_log_up_to_date(uint64_t last_log_index, uint64_t last_log_term) const;
    size_t get_majority_count() const;
    
    // 持久化
    void persist_state();
    bool load_persisted_state();
};

// 网络接口抽象
class RaftNetworkInterface {
public:
    virtual ~RaftNetworkInterface() = default;
    
    // 发送消息
    virtual bool send_message(const std::string& to, const RaftMessage& message) = 0;
    
    // 设置消息处理回调
    virtual void set_message_handler(std::function<void(const RaftMessage&)> handler) = 0;
    
    // 启动和停止网络服务
    virtual bool start(const std::string& address, uint16_t port) = 0;
    virtual void stop() = 0;
    
    // 节点管理
    virtual bool add_peer(const std::string& node_id, const std::string& address, uint16_t port) = 0;
    virtual bool remove_peer(const std::string& node_id) = 0;
    
    // 网络状态
    virtual bool is_peer_reachable(const std::string& node_id) = 0;
    virtual std::vector<std::string> get_reachable_peers() = 0;
};

// 状态机接口抽象
class RaftStateMachine {
public:
    virtual ~RaftStateMachine() = default;
    
    // 应用日志条目
    virtual std::string apply(const LogEntry& entry) = 0;
    
    // 快照操作
    virtual std::vector<uint8_t> create_snapshot() = 0;
    virtual bool restore_snapshot(const std::vector<uint8_t>& snapshot_data) = 0;
    
    // 状态查询
    virtual uint64_t get_last_applied_index() const = 0;
    virtual void set_last_applied_index(uint64_t index) = 0;
};