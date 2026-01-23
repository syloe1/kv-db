#include "raft_node.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <future>

RaftNode::RaftNode(const RaftConfig& config,
                   std::shared_ptr<RaftNetworkInterface> network,
                   std::shared_ptr<RaftStateMachine> state_machine)
    : config_(config), network_(network), state_machine_(state_machine),
      current_term_(0), state_(RaftState::FOLLOWER),
      commit_index_(0), last_applied_(0), running_(false),
      gen_(rd_()) {
    
    // 初始化集群节点信息
    for (const auto& node_id : config_.cluster_nodes) {
        if (node_id != config_.node_id) {
            cluster_nodes_[node_id] = RaftNodeInfo(node_id, "", 0);
        }
    }
    
    // 重置选举超时
    reset_election_timeout();
    
    // 初始化统计信息
    stats_.state = state_;
    stats_.current_term = current_term_;
    stats_.cluster_size = config_.cluster_nodes.size();
    
    std::cout << "RaftNode " << config_.node_id << " initialized with " 
              << config_.cluster_nodes.size() << " nodes in cluster" << std::endl;
}

RaftNode::~RaftNode() {
    stop();
}

bool RaftNode::start() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (running_.load()) {
        return false;
    }
    
    // 加载持久化状态
    load_persisted_state();
    
    // 设置网络消息处理回调
    network_->set_message_handler([this](const RaftMessage& msg) {
        handle_message(msg);
    });
    
    // 启动网络服务
    if (!network_->start(config_.listen_address, config_.listen_port)) {
        std::cout << "Failed to start network service" << std::endl;
        return false;
    }
    
    // 启动主循环线程
    running_.store(true);
    main_loop_thread_ = std::thread(&RaftNode::main_loop, this);
    heartbeat_thread_ = std::thread(&RaftNode::heartbeat_loop, this);
    
    std::cout << "Raft node " << config_.node_id << " started" << std::endl;
    return true;
}

void RaftNode::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 通知所有等待的线程
    message_queue_cv_.notify_all();
    client_request_cv_.notify_all();
    
    // 等待线程结束
    if (main_loop_thread_.joinable()) {
        main_loop_thread_.join();
    }
    
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    // 停止网络服务
    network_->stop();
    
    // 持久化状态
    persist_state();
    
    std::cout << "Raft node " << config_.node_id << " stopped" << std::endl;
}

bool RaftNode::is_running() const {
    return running_.load();
}

ClientResponse RaftNode::handle_client_request(const ClientRequest& request) {
    if (!is_leader()) {
        ClientResponse response;
        response.request_id = request.request_id;
        response.result = ClientRequestResult::NOT_LEADER;
        response.leader_hint = get_leader_id();
        return response;
    }
    
    // 将请求加入队列
    std::promise<ClientResponse> promise;
    auto future = promise.get_future();
    
    {
        std::lock_guard<std::mutex> lock(client_request_mutex_);
        client_request_queue_.emplace(request, std::move(promise));
    }
    client_request_cv_.notify_one();
    
    // 等待处理结果
    auto status = future.wait_for(std::chrono::seconds(5));
    if (status == std::future_status::timeout) {
        ClientResponse response;
        response.request_id = request.request_id;
        response.result = ClientRequestResult::TIMEOUT;
        return response;
    }
    
    return future.get();
}

bool RaftNode::is_leader() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_ == RaftState::LEADER;
}

std::string RaftNode::get_leader_id() const {
    std::lock_guard<std::mutex> lock(cluster_mutex_);
    return current_leader_;
}

void RaftNode::handle_message(const RaftMessage& message) {
    {
        std::lock_guard<std::mutex> lock(message_queue_mutex_);
        message_queue_.push(message);
    }
    message_queue_cv_.notify_one();
}

RaftState RaftNode::get_state() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

uint64_t RaftNode::get_current_term() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_term_;
}

RaftStats RaftNode::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

// 主循环实现
void RaftNode::main_loop() {
    while (running_.load()) {
        // 处理消息队列
        std::unique_lock<std::mutex> msg_lock(message_queue_mutex_);
        message_queue_cv_.wait_for(msg_lock, std::chrono::milliseconds(10), 
                                  [this] { return !message_queue_.empty() || !running_.load(); });
        
        while (!message_queue_.empty() && running_.load()) {
            RaftMessage message = message_queue_.front();
            message_queue_.pop();
            msg_lock.unlock();
            
            // 处理消息
            switch (message.type) {
                case RaftMessageType::REQUEST_VOTE:
                    handle_request_vote(message.request_vote, message.from);
                    break;
                case RaftMessageType::REQUEST_VOTE_REPLY:
                    handle_request_vote_reply(message.request_vote_reply, message.from);
                    break;
                case RaftMessageType::APPEND_ENTRIES:
                    handle_append_entries(message.append_entries, message.from);
                    break;
                case RaftMessageType::APPEND_ENTRIES_REPLY:
                    handle_append_entries_reply(message.append_entries_reply, message.from);
                    break;
                case RaftMessageType::INSTALL_SNAPSHOT:
                    handle_install_snapshot(message.install_snapshot, message.from);
                    break;
                case RaftMessageType::INSTALL_SNAPSHOT_REPLY:
                    handle_install_snapshot_reply(message.install_snapshot_reply, message.from);
                    break;
            }
            
            msg_lock.lock();
        }
        msg_lock.unlock();
        
        // 检查选举超时
        bool should_start_election = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if ((state_ == RaftState::FOLLOWER || state_ == RaftState::CANDIDATE) && 
                is_election_timeout()) {
                std::cout << "Node " << config_.node_id << " election timeout, starting election" << std::endl;
                should_start_election = true;
            }
        }
        
        if (should_start_election) {
            start_election();
        }
        
        // 处理客户端请求（仅领导者）
        if (is_leader()) {
            process_client_requests();
        }
        
        // 应用已提交的日志条目
        apply_committed_entries();
        
        // 更新统计信息
        update_statistics();
    }
}

void RaftNode::heartbeat_loop() {
    while (running_.load()) {
        if (is_leader()) {
            send_heartbeats();
        }
        
        std::this_thread::sleep_for(config_.heartbeat_interval);
    }
}

void RaftNode::become_follower(uint64_t term) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (term > current_term_) {
        current_term_ = term;
        voted_for_.clear();
        persist_state();
    }
    
    state_ = RaftState::FOLLOWER;
    current_leader_.clear();
    votes_received_.clear();
    
    reset_election_timeout();
    
    std::cout << "Node " << config_.node_id << " became follower for term " << current_term_ << std::endl;
}

void RaftNode::become_candidate() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    state_ = RaftState::CANDIDATE;
    current_term_++;
    voted_for_ = config_.node_id;
    votes_received_.clear();
    votes_received_.insert(config_.node_id);
    
    reset_election_timeout();
    persist_state();
    
    stats_.elections_started++;
    
    std::cout << "Node " << config_.node_id << " became candidate for term " << current_term_ << std::endl;
    
    // 检查是否立即获得多数票（单节点情况）
    if (votes_received_.size() >= get_majority_count()) {
        std::cout << "Node " << config_.node_id << " has majority votes, becoming leader" << std::endl;
        become_leader();
        return;
    }
}

void RaftNode::become_leader() {
    // 注意：这个方法假设调用者已经持有state_mutex_锁
    
    state_ = RaftState::LEADER;
    current_leader_ = config_.node_id;
    
    // 初始化领导者状态
    uint64_t next_index = get_last_log_index() + 1;
    for (const auto& node_id : config_.cluster_nodes) {
        if (node_id != config_.node_id) {
            next_index_[node_id] = next_index;
            match_index_[node_id] = 0;
        }
    }
    
    stats_.elections_won++;
    
    std::cout << "Node " << config_.node_id << " became leader for term " << current_term_ << std::endl;
    
    // 立即发送心跳（不需要锁，因为send_heartbeats会处理）
    // 使用异步方式避免死锁
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        send_heartbeats();
    }).detach();
}

void RaftNode::start_election() {
    std::cout << "Node " << config_.node_id << " starting election..." << std::endl;
    become_candidate();
    
    // 发送投票请求给其他节点
    RequestVoteMessage vote_msg;
    vote_msg.term = current_term_;
    vote_msg.candidate_id = config_.node_id;
    vote_msg.last_log_index = get_last_log_index();
    vote_msg.last_log_term = get_last_log_term();
    
    RaftMessage message;
    message.type = RaftMessageType::REQUEST_VOTE;
    message.from = config_.node_id;
    message.request_vote = vote_msg;
    
    // 只向其他节点发送投票请求
    for (const auto& node_id : config_.cluster_nodes) {
        if (node_id != config_.node_id) {
            RaftMessage msg = message;
            msg.to = node_id;
            send_message(node_id, msg);
        }
    }
    
    std::cout << "Node " << config_.node_id << " sent vote requests for term " << current_term_ << std::endl;
}

void RaftNode::handle_request_vote(const RequestVoteMessage& msg, const std::string& from) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    RequestVoteReply reply;
    reply.term = current_term_;
    reply.vote_granted = false;
    
    // 如果请求的任期号小于当前任期号，拒绝投票
    if (msg.term < current_term_) {
        // 发送回复
        RaftMessage response;
        response.type = RaftMessageType::REQUEST_VOTE_REPLY;
        response.from = config_.node_id;
        response.to = from;
        response.request_vote_reply = reply;
        send_message(from, response);
        return;
    }
    
    // 如果请求的任期号大于当前任期号，更新当前任期号并转为跟随者
    if (msg.term > current_term_) {
        become_follower(msg.term);
        reply.term = current_term_;
    }
    
    // 检查是否可以投票
    bool can_vote = (voted_for_.empty() || voted_for_ == msg.candidate_id) &&
                    is_log_up_to_date(msg.last_log_index, msg.last_log_term);
    
    if (can_vote) {
        voted_for_ = msg.candidate_id;
        reply.vote_granted = true;
        reset_election_timeout();
        persist_state();
        
        stats_.votes_granted++;
        
        std::cout << "Node " << config_.node_id << " voted for " << msg.candidate_id 
                  << " in term " << msg.term << std::endl;
    }
    
    // 发送回复
    RaftMessage response;
    response.type = RaftMessageType::REQUEST_VOTE_REPLY;
    response.from = config_.node_id;
    response.to = from;
    response.request_vote_reply = reply;
    send_message(from, response);
}

void RaftNode::handle_request_vote_reply(const RequestVoteReply& msg, const std::string& from) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // 只有候选者才处理投票回复
    if (state_ != RaftState::CANDIDATE) {
        return;
    }
    
    // 如果回复的任期号大于当前任期号，转为跟随者
    if (msg.term > current_term_) {
        become_follower(msg.term);
        return;
    }
    
    // 如果回复的任期号小于当前任期号，忽略
    if (msg.term < current_term_) {
        return;
    }
    
    // 统计投票
    if (msg.vote_granted) {
        votes_received_.insert(from);
        stats_.votes_received++;
        
        std::cout << "Node " << config_.node_id << " received vote from " << from 
                  << " (" << votes_received_.size() << "/" << config_.cluster_nodes.size() << ")" << std::endl;
        
        // 检查是否获得多数票
        if (votes_received_.size() >= get_majority_count()) {
            become_leader();
        }
    }
}

void RaftNode::send_heartbeats() {
    for (const auto& node_id : config_.cluster_nodes) {
        if (node_id != config_.node_id) {
            send_append_entries(node_id);
        }
    }
    
    stats_.heartbeats_sent++;
}

void RaftNode::send_append_entries(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (state_ != RaftState::LEADER) {
        return;
    }
    
    AppendEntriesMessage append_msg;
    append_msg.term = current_term_;
    append_msg.leader_id = config_.node_id;
    append_msg.leader_commit = commit_index_;
    
    uint64_t next_idx = next_index_[node_id];
    append_msg.prev_log_index = next_idx - 1;
    append_msg.prev_log_term = get_log_term(append_msg.prev_log_index);
    
    // 添加日志条目（如果有的话）
    uint64_t last_idx = get_last_log_index();
    if (next_idx <= last_idx) {
        size_t max_entries = std::min(static_cast<size_t>(last_idx - next_idx + 1),
                                     config_.max_log_entries_per_request);
        
        for (size_t i = 0; i < max_entries; i++) {
            append_msg.entries.push_back(get_log_entry(next_idx + i));
        }
    }
    
    RaftMessage message;
    message.type = RaftMessageType::APPEND_ENTRIES;
    message.from = config_.node_id;
    message.to = node_id;
    message.append_entries = append_msg;
    
    send_message(node_id, message);
    stats_.append_entries_sent++;
}

// 工具方法实现
bool RaftNode::is_election_timeout() const {
    auto now = std::chrono::system_clock::now();
    return (now - last_heartbeat_) > election_timeout_;
}

void RaftNode::reset_election_timeout() {
    last_heartbeat_ = std::chrono::system_clock::now();
    election_timeout_ = config_.get_random_election_timeout();
}

size_t RaftNode::get_majority_count() const {
    return (config_.cluster_nodes.size() / 2) + 1;
}

uint64_t RaftNode::get_last_log_index() const {
    return log_.empty() ? 0 : log_.back().index;
}

uint64_t RaftNode::get_last_log_term() const {
    return log_.empty() ? 0 : log_.back().term;
}

uint64_t RaftNode::get_log_term(uint64_t index) const {
    if (index == 0) return 0;
    
    for (const auto& entry : log_) {
        if (entry.index == index) {
            return entry.term;
        }
    }
    
    return 0;
}

bool RaftNode::is_log_up_to_date(uint64_t last_log_index, uint64_t last_log_term) const {
    uint64_t my_last_term = get_last_log_term();
    uint64_t my_last_index = get_last_log_index();
    
    if (last_log_term != my_last_term) {
        return last_log_term > my_last_term;
    }
    
    return last_log_index >= my_last_index;
}

void RaftNode::send_message(const std::string& to, const RaftMessage& message) {
    network_->send_message(to, message);
}

void RaftNode::broadcast_message(const RaftMessage& message) {
    for (const auto& node_id : config_.cluster_nodes) {
        if (node_id != config_.node_id) {
            RaftMessage msg = message;
            msg.to = node_id;
            send_message(node_id, msg);
        }
    }
}

void RaftNode::update_statistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    stats_.state = state_;
    stats_.current_term = current_term_;
    stats_.voted_for = voted_for_;
    stats_.leader_id = current_leader_;
    stats_.log_length = log_.size();
    stats_.commit_index = commit_index_;
    stats_.last_applied = last_applied_;
    stats_.cluster_size = config_.cluster_nodes.size();
    
    // 计算活跃节点数
    stats_.active_nodes = 1; // 自己总是活跃的
    for (const auto& pair : cluster_nodes_) {
        if (pair.second.is_alive) {
            stats_.active_nodes++;
        }
    }
}

// 简化的持久化实现
void RaftNode::persist_state() {
    // 这里应该实现真正的持久化逻辑
    // 为了简化，暂时不实现
}

bool RaftNode::load_persisted_state() {
    // 这里应该实现真正的加载逻辑
    // 为了简化，暂时不实现
    return true;
}

// 其他方法的简化实现
void RaftNode::handle_append_entries(const AppendEntriesMessage& msg, const std::string& from) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    stats_.append_entries_received++;
    
    AppendEntriesReply reply;
    reply.term = current_term_;
    reply.success = false;
    reply.match_index = 0;
    
    // 如果请求的任期号小于当前任期号，拒绝
    if (msg.term < current_term_) {
        RaftMessage response;
        response.type = RaftMessageType::APPEND_ENTRIES_REPLY;
        response.from = config_.node_id;
        response.to = from;
        response.append_entries_reply = reply;
        send_message(from, response);
        return;
    }
    
    // 如果请求的任期号大于等于当前任期号，更新状态
    if (msg.term >= current_term_) {
        if (msg.term > current_term_) {
            current_term_ = msg.term;
            voted_for_.clear();
            persist_state();
        }
        
        // 转为跟随者状态
        if (state_ != RaftState::FOLLOWER) {
            state_ = RaftState::FOLLOWER;
        }
        
        // 更新领导者信息
        current_leader_ = msg.leader_id;
        reset_election_timeout();
        
        // 这是有效的心跳
        if (msg.entries.empty()) {
            stats_.heartbeats_received++;
        }
    }
    
    reply.term = current_term_;
    
    // 检查日志一致性
    if (msg.prev_log_index > 0) {
        // 检查前一个日志条目是否存在且任期匹配
        if (msg.prev_log_index > get_last_log_index() || 
            get_log_term(msg.prev_log_index) != msg.prev_log_term) {
            // 日志不一致，拒绝
            reply.success = false;
            reply.match_index = get_last_log_index();
            
            RaftMessage response;
            response.type = RaftMessageType::APPEND_ENTRIES_REPLY;
            response.from = config_.node_id;
            response.to = from;
            response.append_entries_reply = reply;
            send_message(from, response);
            return;
        }
    }
    
    // 追加新的日志条目
    if (!msg.entries.empty()) {
        if (append_log_entries(msg.entries, msg.prev_log_index, msg.prev_log_term)) {
            reply.success = true;
            reply.match_index = get_last_log_index();
        } else {
            reply.success = false;
            reply.match_index = get_last_log_index();
        }
    } else {
        // 心跳消息，成功
        reply.success = true;
        reply.match_index = get_last_log_index();
    }
    
    // 更新提交索引
    if (msg.leader_commit > commit_index_) {
        commit_index_ = std::min(msg.leader_commit, get_last_log_index());
    }
    
    // 发送回复
    RaftMessage response;
    response.type = RaftMessageType::APPEND_ENTRIES_REPLY;
    response.from = config_.node_id;
    response.to = from;
    response.append_entries_reply = reply;
    send_message(from, response);
}

void RaftNode::handle_append_entries_reply(const AppendEntriesReply& msg, const std::string& from) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // 只有领导者才处理追加日志回复
    if (state_ != RaftState::LEADER) {
        return;
    }
    
    // 如果回复的任期号大于当前任期号，转为跟随者
    if (msg.term > current_term_) {
        become_follower(msg.term);
        return;
    }
    
    // 如果回复的任期号小于当前任期号，忽略
    if (msg.term < current_term_) {
        return;
    }
    
    // 处理成功的回复
    if (msg.success) {
        // 更新匹配索引和下一个索引
        match_index_[from] = msg.match_index;
        next_index_[from] = msg.match_index + 1;
        
        // 尝试更新提交索引
        update_commit_index();
        
        std::cout << "Node " << config_.node_id << " received successful append_entries_reply from " 
                  << from << ", match_index: " << msg.match_index << std::endl;
    } else {
        // 处理失败的回复，减少下一个索引并重试
        if (next_index_[from] > 1) {
            next_index_[from]--;
        }
        
        std::cout << "Node " << config_.node_id << " received failed append_entries_reply from " 
                  << from << ", decreasing next_index to: " << next_index_[from] << std::endl;
        
        // 立即重试发送日志条目
        send_append_entries(from);
    }
}

void RaftNode::handle_install_snapshot(const InstallSnapshotMessage& msg, const std::string& from) {
    // 简化实现
}

void RaftNode::handle_install_snapshot_reply(const InstallSnapshotReply& msg, const std::string& from) {
    // 简化实现
}

void RaftNode::process_client_requests() {
    std::unique_lock<std::mutex> lock(client_request_mutex_);
    
    while (!client_request_queue_.empty() && running_.load()) {
        auto request_pair = std::move(client_request_queue_.front());
        client_request_queue_.pop();
        lock.unlock();
        
        ClientRequest request = request_pair.first;
        std::promise<ClientResponse> promise = std::move(request_pair.second);
        
        ClientResponse response = process_client_request(request);
        promise.set_value(response);
        
        lock.lock();
    }
}

ClientResponse RaftNode::process_client_request(const ClientRequest& request) {
    ClientResponse response;
    response.request_id = request.request_id;
    
    // 检查是否仍然是领导者
    if (!is_leader()) {
        response.result = ClientRequestResult::NOT_LEADER;
        response.leader_hint = get_leader_id();
        return response;
    }
    
    try {
        // 创建日志条目
        LogEntry entry(current_term_, get_last_log_index() + 1, request.command);
        
        // 追加到本地日志
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            log_.push_back(entry);
            persist_state();
        }
        
        std::cout << "Node " << config_.node_id << " appended log entry: " 
                  << entry.index << " with command: " << entry.command << std::endl;
        
        // 立即发送给所有跟随者
        for (const auto& node_id : config_.cluster_nodes) {
            if (node_id != config_.node_id) {
                send_append_entries(node_id);
            }
        }
        
        // 等待大多数节点确认（简化实现，实际应该有超时机制）
        auto start_time = std::chrono::system_clock::now();
        auto timeout = std::chrono::seconds(2);
        
        while (std::chrono::system_clock::now() - start_time < timeout) {
            // 检查是否已经提交
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (commit_index_ >= entry.index) {
                    response.result = ClientRequestResult::SUCCESS;
                    response.response_data = "Command executed successfully";
                    return response;
                }
            }
            
            // 检查是否仍然是领导者
            if (!is_leader()) {
                response.result = ClientRequestResult::NOT_LEADER;
                response.leader_hint = get_leader_id();
                return response;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 超时
        response.result = ClientRequestResult::TIMEOUT;
        response.response_data = "Request timeout";
        
    } catch (const std::exception& e) {
        response.result = ClientRequestResult::INTERNAL_ERROR;
        response.response_data = std::string("Internal error: ") + e.what();
    }
    
    return response;
}

void RaftNode::apply_committed_entries() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // 应用所有已提交但未应用的日志条目
    while (last_applied_ < commit_index_ && running_.load()) {
        last_applied_++;
        
        // 查找要应用的日志条目
        LogEntry entry_to_apply;
        bool found = false;
        
        for (const auto& entry : log_) {
            if (entry.index == last_applied_) {
                entry_to_apply = entry;
                found = true;
                break;
            }
        }
        
        if (found) {
            try {
                // 应用到状态机
                std::string result = state_machine_->apply(entry_to_apply);
                
                std::cout << "Node " << config_.node_id << " applied log entry " 
                          << entry_to_apply.index << " with command: " 
                          << entry_to_apply.command << ", result: " << result << std::endl;
                
                // 更新状态机的最后应用索引
                state_machine_->set_last_applied_index(last_applied_);
                
            } catch (const std::exception& e) {
                std::cout << "Error applying log entry " << entry_to_apply.index 
                          << ": " << e.what() << std::endl;
                // 在实际实现中，这里可能需要更复杂的错误处理
            }
        } else {
            std::cout << "Warning: Could not find log entry with index " << last_applied_ << std::endl;
            break;
        }
    }
}

LogEntry RaftNode::get_log_entry(uint64_t index) const {
    for (const auto& entry : log_) {
        if (entry.index == index) {
            return entry;
        }
    }
    return LogEntry();
}

bool RaftNode::append_log_entry(const LogEntry& entry) {
    try {
        log_.push_back(entry);
        persist_state();
        return true;
    } catch (const std::exception& e) {
        std::cout << "Error appending log entry: " << e.what() << std::endl;
        return false;
    }
}

bool RaftNode::append_log_entries(const std::vector<LogEntry>& entries, 
                                 uint64_t prev_log_index, uint64_t prev_log_term) {
    try {
        // 如果有冲突的日志条目，删除它们及其后续条目
        if (!entries.empty()) {
            uint64_t first_new_index = entries[0].index;
            
            // 删除从first_new_index开始的所有日志条目
            auto it = log_.begin();
            while (it != log_.end()) {
                if (it->index >= first_new_index) {
                    it = log_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // 追加新的日志条目
        for (const auto& entry : entries) {
            log_.push_back(entry);
        }
        
        persist_state();
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "Error appending log entries: " << e.what() << std::endl;
        return false;
    }
}

bool RaftNode::add_node(const std::string& node_id, const std::string& address, uint16_t port) {
    std::lock_guard<std::mutex> lock(cluster_mutex_);
    
    // 检查节点是否已存在
    if (cluster_nodes_.find(node_id) != cluster_nodes_.end()) {
        return false;
    }
    
    // 添加新节点
    cluster_nodes_[node_id] = RaftNodeInfo(node_id, address, port);
    
    // 如果是领导者，初始化该节点的索引
    if (is_leader()) {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        next_index_[node_id] = get_last_log_index() + 1;
        match_index_[node_id] = 0;
    }
    
    // 添加到网络层
    network_->add_peer(node_id, address, port);
    
    std::cout << "Added node " << node_id << " to cluster" << std::endl;
    return true;
}

bool RaftNode::remove_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(cluster_mutex_);
    
    // 检查节点是否存在
    auto it = cluster_nodes_.find(node_id);
    if (it == cluster_nodes_.end()) {
        return false;
    }
    
    // 移除节点
    cluster_nodes_.erase(it);
    
    // 如果是领导者，清理该节点的索引
    if (is_leader()) {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        next_index_.erase(node_id);
        match_index_.erase(node_id);
    }
    
    // 从网络层移除
    network_->remove_peer(node_id);
    
    std::cout << "Removed node " << node_id << " from cluster" << std::endl;
    return true;
}

std::vector<RaftNodeInfo> RaftNode::get_cluster_nodes() const {
    std::lock_guard<std::mutex> lock(cluster_mutex_);
    
    std::vector<RaftNodeInfo> nodes;
    for (const auto& pair : cluster_nodes_) {
        nodes.push_back(pair.second);
    }
    
    return nodes;
}

void RaftNode::update_commit_index() {
    if (state_ != RaftState::LEADER) {
        return;
    }
    
    // 找到大多数节点都已复制的最高日志索引
    std::vector<uint64_t> match_indices;
    match_indices.push_back(get_last_log_index()); // 领导者自己的日志索引
    
    for (const auto& pair : match_index_) {
        match_indices.push_back(pair.second);
    }
    
    // 排序以找到中位数
    std::sort(match_indices.begin(), match_indices.end(), std::greater<uint64_t>());
    
    size_t majority_index = (match_indices.size() - 1) / 2;
    uint64_t new_commit_index = match_indices[majority_index];
    
    // 只能提交当前任期的日志条目
    if (new_commit_index > commit_index_ && get_log_term(new_commit_index) == current_term_) {
        commit_index_ = new_commit_index;
        
        std::cout << "Node " << config_.node_id << " updated commit_index to " 
                  << commit_index_ << std::endl;
    }
}