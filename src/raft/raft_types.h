#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>
#include <random>

// Raft节点状态
enum class RaftState {
    FOLLOWER,   // 跟随者
    CANDIDATE,  // 候选者
    LEADER      // 领导者
};

// Raft消息类型
enum class RaftMessageType {
    REQUEST_VOTE,        // 请求投票
    REQUEST_VOTE_REPLY,  // 投票回复
    APPEND_ENTRIES,      // 追加日志条目
    APPEND_ENTRIES_REPLY,// 追加日志回复
    INSTALL_SNAPSHOT,    // 安装快照
    INSTALL_SNAPSHOT_REPLY // 安装快照回复
};

// 日志条目
struct LogEntry {
    uint64_t term;          // 任期号
    uint64_t index;         // 日志索引
    std::string command;    // 命令内容
    std::chrono::system_clock::time_point timestamp; // 时间戳
    
    LogEntry() : term(0), index(0) {}
    
    LogEntry(uint64_t t, uint64_t idx, const std::string& cmd)
        : term(t), index(idx), command(cmd), 
          timestamp(std::chrono::system_clock::now()) {}
    
    // 序列化
    std::vector<uint8_t> serialize() const;
    static LogEntry deserialize(const std::vector<uint8_t>& data);
};

// 请求投票消息
struct RequestVoteMessage {
    uint64_t term;           // 候选者的任期号
    std::string candidate_id; // 候选者ID
    uint64_t last_log_index; // 候选者最后日志条目的索引
    uint64_t last_log_term;  // 候选者最后日志条目的任期号
    
    RequestVoteMessage() : term(0), last_log_index(0), last_log_term(0) {}
};

// 投票回复消息
struct RequestVoteReply {
    uint64_t term;        // 当前任期号
    bool vote_granted;    // 是否投票给候选者
    
    RequestVoteReply() : term(0), vote_granted(false) {}
};

// 追加日志条目消息
struct AppendEntriesMessage {
    uint64_t term;              // 领导者的任期号
    std::string leader_id;      // 领导者ID
    uint64_t prev_log_index;    // 新日志条目前一条的索引
    uint64_t prev_log_term;     // 新日志条目前一条的任期号
    std::vector<LogEntry> entries; // 要存储的日志条目
    uint64_t leader_commit;     // 领导者已提交的日志索引
    
    AppendEntriesMessage() : term(0), prev_log_index(0), 
                           prev_log_term(0), leader_commit(0) {}
};

// 追加日志回复消息
struct AppendEntriesReply {
    uint64_t term;        // 当前任期号
    bool success;         // 是否成功追加日志
    uint64_t match_index; // 匹配的日志索引
    
    AppendEntriesReply() : term(0), success(false), match_index(0) {}
};

// 安装快照消息
struct InstallSnapshotMessage {
    uint64_t term;              // 领导者的任期号
    std::string leader_id;      // 领导者ID
    uint64_t last_included_index; // 快照中包含的最后日志条目的索引
    uint64_t last_included_term;  // 快照中包含的最后日志条目的任期号
    uint64_t offset;            // 分块传输的偏移量
    std::vector<uint8_t> data;  // 快照数据
    bool done;                  // 是否是最后一块
    
    InstallSnapshotMessage() : term(0), last_included_index(0), 
                             last_included_term(0), offset(0), done(false) {}
};

// 安装快照回复消息
struct InstallSnapshotReply {
    uint64_t term;        // 当前任期号
    
    InstallSnapshotReply() : term(0) {}
};

// Raft消息包装器
struct RaftMessage {
    RaftMessageType type;
    std::string from;
    std::string to;
    
    // 消息内容（使用union或variant会更好，这里简化）
    RequestVoteMessage request_vote;
    RequestVoteReply request_vote_reply;
    AppendEntriesMessage append_entries;
    AppendEntriesReply append_entries_reply;
    InstallSnapshotMessage install_snapshot;
    InstallSnapshotReply install_snapshot_reply;
    
    RaftMessage() : type(RaftMessageType::REQUEST_VOTE) {}
    
    // 序列化
    std::vector<uint8_t> serialize() const;
    static RaftMessage deserialize(const std::vector<uint8_t>& data);
};

// Raft配置
struct RaftConfig {
    std::string node_id;                    // 节点ID
    std::vector<std::string> cluster_nodes; // 集群节点列表
    
    // 超时配置
    std::chrono::milliseconds election_timeout_min{150};  // 选举超时最小值
    std::chrono::milliseconds election_timeout_max{300};  // 选举超时最大值
    std::chrono::milliseconds heartbeat_interval{50};     // 心跳间隔
    std::chrono::milliseconds rpc_timeout{100};           // RPC超时
    
    // 日志配置
    size_t max_log_entries_per_request{100};  // 每次请求最大日志条目数
    size_t snapshot_threshold{1000};          // 快照阈值
    
    // 网络配置
    std::string listen_address{"0.0.0.0"};   // 监听地址
    uint16_t listen_port{8080};               // 监听端口
    
    RaftConfig() = default;
    
    // 验证配置
    bool is_valid() const {
        return !node_id.empty() && 
               !cluster_nodes.empty() &&
               listen_port > 0 &&
               election_timeout_min < election_timeout_max &&
               heartbeat_interval < election_timeout_min;
    }
    
    // 获取随机选举超时时间
    std::chrono::milliseconds get_random_election_timeout() const {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(
            election_timeout_min.count(), 
            election_timeout_max.count()
        );
        return std::chrono::milliseconds(dis(gen));
    }
};

// Raft节点信息
struct RaftNodeInfo {
    std::string node_id;
    std::string address;
    uint16_t port;
    bool is_alive;
    std::chrono::system_clock::time_point last_contact;
    
    RaftNodeInfo() : port(0), is_alive(false) {}
    
    RaftNodeInfo(const std::string& id, const std::string& addr, uint16_t p)
        : node_id(id), address(addr), port(p), is_alive(true),
          last_contact(std::chrono::system_clock::now()) {}
};

// Raft统计信息
struct RaftStats {
    RaftState state;
    uint64_t current_term;
    std::string voted_for;
    std::string leader_id;
    
    uint64_t log_length;
    uint64_t commit_index;
    uint64_t last_applied;
    
    size_t cluster_size;
    size_t active_nodes;
    
    uint64_t elections_started;
    uint64_t elections_won;
    uint64_t votes_received;
    uint64_t votes_granted;
    
    uint64_t append_entries_sent;
    uint64_t append_entries_received;
    uint64_t heartbeats_sent;
    uint64_t heartbeats_received;
    
    RaftStats() : state(RaftState::FOLLOWER), current_term(0),
                 log_length(0), commit_index(0), last_applied(0),
                 cluster_size(0), active_nodes(0),
                 elections_started(0), elections_won(0),
                 votes_received(0), votes_granted(0),
                 append_entries_sent(0), append_entries_received(0),
                 heartbeats_sent(0), heartbeats_received(0) {}
};

// 客户端请求结果
enum class ClientRequestResult {
    SUCCESS,           // 成功
    NOT_LEADER,        // 不是领导者
    TIMEOUT,           // 超时
    NETWORK_ERROR,     // 网络错误
    INTERNAL_ERROR     // 内部错误
};

// 客户端请求
struct ClientRequest {
    std::string request_id;  // 请求ID
    std::string command;     // 命令
    std::chrono::system_clock::time_point timestamp; // 时间戳
    
    ClientRequest() : timestamp(std::chrono::system_clock::now()) {}
    
    ClientRequest(const std::string& id, const std::string& cmd)
        : request_id(id), command(cmd), 
          timestamp(std::chrono::system_clock::now()) {}
};

// 客户端响应
struct ClientResponse {
    std::string request_id;
    ClientRequestResult result;
    std::string response_data;
    std::string leader_hint;  // 如果不是领导者，提示真正的领导者
    
    ClientResponse() : result(ClientRequestResult::INTERNAL_ERROR) {}
};