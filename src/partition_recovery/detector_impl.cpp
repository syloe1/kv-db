#include "failure_detector.h"
#include <iostream>
#include <algorithm>
#include <sstream>

FailureDetector::FailureDetector(const std::string& node_id,
                                const FailureDetectorConfig& config,
                                std::shared_ptr<PartitionRecoveryNetworkInterface> network)
    : node_id_(node_id), config_(config), network_(network),
      current_partition_state_(PartitionState::NORMAL), running_(false),
      next_sequence_number_(1) {
    
    if (!config_.is_valid()) {
        throw std::invalid_argument("Invalid failure detector configuration");
    }
    
    std::cout << "FailureDetector " << node_id_ << " initialized" << std::endl;
}

FailureDetector::~FailureDetector() {
    stop();
}

bool FailureDetector::start() {
    if (running_.load()) {
        return false;
    }
    
    // 设置网络消息处理回调
    network_->set_message_handler([this](const PartitionDetectionMessage& msg) {
        handle_message(msg);
    });
    
    // 启动网络服务
    if (!network_->start("0.0.0.0", 8080)) {
        std::cout << "Failed to start failure detector network" << std::endl;
        return false;
    }
    
    running_.store(true);
    
    // 启动工作线程
    detector_thread_ = std::thread(&FailureDetector::detector_main_loop, this);
    heartbeat_thread_ = std::thread(&FailureDetector::heartbeat_loop, this);
    analysis_thread_ = std::thread(&FailureDetector::analysis_loop, this);
    
    std::cout << "FailureDetector " << node_id_ << " started" << std::endl;
    return true;
}

void FailureDetector::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 通知所有等待的线程
    message_queue_cv_.notify_all();
    
    // 等待线程结束
    if (detector_thread_.joinable()) {
        detector_thread_.join();
    }
    
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    if (analysis_thread_.joinable()) {
        analysis_thread_.join();
    }
    
    // 停止网络服务
    network_->stop();
    
    std::cout << "FailureDetector " << node_id_ << " stopped" << std::endl;
}

bool FailureDetector::is_running() const {
    return running_.load();
}

bool FailureDetector::add_node(const std::string& node_id, const std::string& address, uint16_t port) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    NodeInfo node(node_id, address, port);
    monitored_nodes_[node_id] = node;
    
    // 添加到网络层
    network_->add_node(node_id, address, port);
    
    std::cout << "Added monitored node " << node_id << " at " << address << ":" << port << std::endl;
    return true;
}

bool FailureDetector::remove_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto it = monitored_nodes_.find(node_id);
    if (it == monitored_nodes_.end()) {
        return false;
    }
    
    monitored_nodes_.erase(it);
    
    // 从网络层移除
    network_->remove_node(node_id);
    
    std::cout << "Removed monitored node " << node_id << std::endl;
    return true;
}

std::vector<NodeInfo> FailureDetector::get_monitored_nodes() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    std::vector<NodeInfo> nodes;
    for (const auto& pair : monitored_nodes_) {
        nodes.push_back(pair.second);
    }
    
    return nodes;
}

bool FailureDetector::is_node_healthy(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto it = monitored_nodes_.find(node_id);
    if (it == monitored_nodes_.end()) {
        return false;
    }
    
    return it->second.is_healthy();
}

bool FailureDetector::is_node_reachable(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto it = monitored_nodes_.find(node_id);
    if (it == monitored_nodes_.end()) {
        return false;
    }
    
    return it->second.is_reachable(config_.failure_timeout);
}

std::vector<std::string> FailureDetector::get_healthy_nodes() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    std::vector<std::string> healthy_nodes;
    for (const auto& pair : monitored_nodes_) {
        if (pair.second.is_healthy()) {
            healthy_nodes.push_back(pair.first);
        }
    }
    
    return healthy_nodes;
}

std::vector<std::string> FailureDetector::get_failed_nodes() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    std::vector<std::string> failed_nodes;
    for (const auto& pair : monitored_nodes_) {
        if (!pair.second.is_healthy()) {
            failed_nodes.push_back(pair.first);
        }
    }
    
    return failed_nodes;
}

bool FailureDetector::is_partitioned() const {
    std::lock_guard<std::mutex> lock(partitions_mutex_);
    return current_partition_state_ == PartitionState::PARTITIONED;
}

std::vector<PartitionInfo> FailureDetector::get_detected_partitions() const {
    std::lock_guard<std::mutex> lock(partitions_mutex_);
    return detected_partitions_;
}

void FailureDetector::handle_message(const PartitionDetectionMessage& message) {
    {
        std::lock_guard<std::mutex> lock(message_queue_mutex_);
        message_queue_.push(message);
    }
    message_queue_cv_.notify_one();
}

void FailureDetector::detector_main_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(message_queue_mutex_);
        message_queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                                  [this] { return !message_queue_.empty() || !running_.load(); });
        
        while (!message_queue_.empty() && running_.load()) {
            PartitionDetectionMessage message = message_queue_.front();
            message_queue_.pop();
            lock.unlock();
            
            // 处理消息
            switch (message.type) {
                case PartitionDetectionMessageType::HEARTBEAT:
                    handle_heartbeat(message);
                    break;
                    
                case PartitionDetectionMessageType::HEARTBEAT_ACK:
                    handle_heartbeat_ack(message);
                    break;
                    
                case PartitionDetectionMessageType::PARTITION_SUSPECT:
                    handle_partition_suspect(message);
                    break;
                    
                case PartitionDetectionMessageType::PARTITION_CONFIRM:
                    handle_partition_confirm(message);
                    break;
                    
                case PartitionDetectionMessageType::HEALTH_CHECK:
                    handle_health_check(message);
                    break;
                    
                case PartitionDetectionMessageType::HEALTH_REPORT:
                    handle_health_report(message);
                    break;
                    
                default:
                    std::cout << "Unknown partition detection message type" << std::endl;
                    break;
            }
            
            lock.lock();
        }
    }
}

void FailureDetector::heartbeat_loop() {
    while (running_.load()) {
        send_heartbeats();
        std::this_thread::sleep_for(config_.heartbeat_interval);
    }
}

void FailureDetector::analysis_loop() {
    while (running_.load()) {
        detect_node_failures();
        detect_network_partitions();
        update_statistics();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void FailureDetector::send_heartbeats() {
    std::vector<std::string> node_ids;
    
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (const auto& pair : monitored_nodes_) {
            node_ids.push_back(pair.first);
        }
    }
    
    for (const auto& node_id : node_ids) {
        send_heartbeat_to_node(node_id);
    }
}

void FailureDetector::send_heartbeat_to_node(const std::string& node_id) {
    PartitionDetectionMessage message;
    message.type = PartitionDetectionMessageType::HEARTBEAT;
    message.sender_id = node_id_;
    message.target_id = node_id;
    message.sequence_number = generate_sequence_number();
    message.timestamp = get_current_timestamp();
    
    // 添加可达节点列表
    message.reachable_nodes.insert(node_id_); // 自己总是可达的
    auto healthy_nodes = get_healthy_nodes();
    for (const auto& healthy_node : healthy_nodes) {
        message.reachable_nodes.insert(healthy_node);
    }
    
    send_message(node_id, message);
}

void FailureDetector::handle_heartbeat(const PartitionDetectionMessage& message) {
    // 更新发送者的心跳信息
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto it = monitored_nodes_.find(message.sender_id);
        if (it != monitored_nodes_.end()) {
            it->second.last_heartbeat = std::chrono::system_clock::now();
            it->second.heartbeat_sequence = message.sequence_number;
            
            // 如果节点之前被标记为故障，现在恢复了
            if (it->second.health_state != NodeHealthState::HEALTHY) {
                mark_node_recovered(message.sender_id);
            }
        }
    }
    
    // 发送心跳确认
    PartitionDetectionMessage ack_message;
    ack_message.type = PartitionDetectionMessageType::HEARTBEAT_ACK;
    ack_message.sender_id = node_id_;
    ack_message.target_id = message.sender_id;
    ack_message.sequence_number = message.sequence_number;
    ack_message.timestamp = get_current_timestamp();
    
    send_message(message.sender_id, ack_message);
}

void FailureDetector::handle_heartbeat_ack(const PartitionDetectionMessage& message) {
    auto response_time = get_current_timestamp() - message.timestamp;
    
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto it = monitored_nodes_.find(message.sender_id);
        if (it != monitored_nodes_.end()) {
            it->second.last_response = std::chrono::system_clock::now();
            it->second.consecutive_failures = 0;
            update_node_response_time(message.sender_id, static_cast<double>(response_time));
            
            // 如果节点之前被标记为故障，现在恢复了
            if (it->second.health_state != NodeHealthState::HEALTHY) {
                mark_node_recovered(message.sender_id);
            }
        }
    }
}

void FailureDetector::detect_node_failures() {
    std::vector<std::string> suspected_failed_nodes;
    
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (auto& pair : monitored_nodes_) {
            if (is_node_suspected_failed(pair.second)) {
                suspected_failed_nodes.push_back(pair.first);
            }
        }
    }
    
    for (const auto& node_id : suspected_failed_nodes) {
        mark_node_failed(node_id, FailureType::NODE_CRASH);
    }
}

void FailureDetector::detect_network_partitions() {
    if (!config_.enable_partition_detection) {
        return;
    }
    
    analyze_network_topology();
}

bool FailureDetector::is_node_suspected_failed(const NodeInfo& node) const {
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - node.last_response);
    
    return elapsed > config_.failure_timeout || 
           node.consecutive_failures >= config_.max_consecutive_failures;
}

void FailureDetector::mark_node_failed(const std::string& node_id, FailureType failure_type) {
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto it = monitored_nodes_.find(node_id);
        if (it != monitored_nodes_.end() && it->second.health_state == NodeHealthState::HEALTHY) {
            it->second.health_state = NodeHealthState::FAILED;
            it->second.consecutive_failures++;
        }
    }
    
    // 调用故障回调
    if (failure_callback_) {
        failure_callback_(node_id, failure_type);
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.node_failures_detected++;
    }
    
    std::cout << "Node " << node_id << " marked as failed (type: " 
              << static_cast<int>(failure_type) << ")" << std::endl;
}

void FailureDetector::mark_node_recovered(const std::string& node_id) {
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto it = monitored_nodes_.find(node_id);
        if (it != monitored_nodes_.end()) {
            it->second.health_state = NodeHealthState::HEALTHY;
            it->second.consecutive_failures = 0;
        }
    }
    
    // 调用恢复回调
    if (recovery_callback_) {
        recovery_callback_(node_id);
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.nodes_recovered++;
    }
    
    std::cout << "Node " << node_id << " recovered" << std::endl;
}

void FailureDetector::analyze_network_topology() {
    auto connected_components = find_connected_components();
    
    if (connected_components.size() > 1) {
        // 检测到网络分区
        std::lock_guard<std::mutex> lock(partitions_mutex_);
        
        detected_partitions_.clear();
        current_partition_state_ = PartitionState::PARTITIONED;
        
        for (size_t i = 0; i < connected_components.size(); i++) {
            PartitionInfo partition;
            partition.partition_id = "partition_" + std::to_string(i);
            partition.state = PartitionState::PARTITIONED;
            partition.nodes = connected_components[i];
            partition.detected_time = std::chrono::system_clock::now();
            partition.strategy = config_.default_recovery_strategy;
            
            size_t total_nodes = 0;
            {
                std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
                total_nodes = monitored_nodes_.size() + 1; // +1 for self
            }
            
            partition.majority_size = total_nodes / 2 + 1;
            
            detected_partitions_.push_back(partition);
            
            // 调用分区回调
            if (partition_callback_) {
                partition_callback_(partition);
            }
        }
        
        // 更新统计信息
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.total_partitions_detected++;
        }
        
        std::cout << "Network partition detected: " << connected_components.size() 
                  << " partitions found" << std::endl;
    } else {
        // 网络正常
        std::lock_guard<std::mutex> lock(partitions_mutex_);
        if (current_partition_state_ == PartitionState::PARTITIONED) {
            current_partition_state_ = PartitionState::RECOVERED;
            
            // 更新统计信息
            {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                stats_.partitions_recovered++;
            }
            
            std::cout << "Network partition recovered" << std::endl;
        }
    }
}

std::vector<std::unordered_set<std::string>> FailureDetector::find_connected_components() {
    std::vector<std::unordered_set<std::string>> components;
    std::unordered_set<std::string> visited;
    
    // 添加自己到第一个组件
    std::unordered_set<std::string> self_component;
    self_component.insert(node_id_);
    
    // 添加所有健康的节点到自己的组件
    auto healthy_nodes = get_healthy_nodes();
    for (const auto& node_id : healthy_nodes) {
        self_component.insert(node_id);
        visited.insert(node_id);
    }
    
    if (!self_component.empty()) {
        components.push_back(self_component);
    }
    
    // 处理不健康的节点（它们形成单独的组件）
    auto failed_nodes = get_failed_nodes();
    for (const auto& node_id : failed_nodes) {
        if (visited.find(node_id) == visited.end()) {
            std::unordered_set<std::string> failed_component;
            failed_component.insert(node_id);
            components.push_back(failed_component);
            visited.insert(node_id);
        }
    }
    
    return components;
}

uint64_t FailureDetector::generate_sequence_number() {
    return next_sequence_number_.fetch_add(1);
}

uint64_t FailureDetector::get_current_timestamp() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void FailureDetector::update_node_response_time(const std::string& node_id, double response_time_ms) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = monitored_nodes_.find(node_id);
    if (it != monitored_nodes_.end()) {
        // 使用指数移动平均计算响应时间
        const double alpha = 0.1;
        it->second.response_time_ms = alpha * response_time_ms + 
                                     (1.0 - alpha) * it->second.response_time_ms;
    }
}

void FailureDetector::update_statistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    // 计算检测准确率
    if (stats_.total_partitions_detected > 0) {
        stats_.partition_recovery_rate = 
            static_cast<double>(stats_.partitions_recovered) / stats_.total_partitions_detected;
    }
    
    if (stats_.false_positives + stats_.false_negatives > 0) {
        size_t total_detections = stats_.total_partitions_detected + stats_.false_positives;
        stats_.detection_accuracy = 
            static_cast<double>(stats_.total_partitions_detected) / total_detections;
    }
}

bool FailureDetector::send_message(const std::string& target_id, const PartitionDetectionMessage& message) {
    return network_->send_message(target_id, message);
}

void FailureDetector::broadcast_message(const PartitionDetectionMessage& message) {
    auto healthy_nodes = get_healthy_nodes();
    network_->broadcast_message(healthy_nodes, message);
}

PartitionRecoveryStats FailureDetector::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void FailureDetector::set_failure_callback(std::function<void(const std::string&, FailureType)> callback) {
    failure_callback_ = callback;
}

void FailureDetector::set_recovery_callback(std::function<void(const std::string&)> callback) {
    recovery_callback_ = callback;
}

void FailureDetector::set_partition_callback(std::function<void(const PartitionInfo&)> callback) {
    partition_callback_ = callback;
}

// 简化的消息处理方法
void FailureDetector::handle_partition_suspect(const PartitionDetectionMessage& message) {
    std::cout << "Received partition suspect from " << message.sender_id << std::endl;
}

void FailureDetector::handle_partition_confirm(const PartitionDetectionMessage& message) {
    std::cout << "Received partition confirm from " << message.sender_id << std::endl;
}

void FailureDetector::handle_health_check(const PartitionDetectionMessage& message) {
    // 发送健康报告
    PartitionDetectionMessage report;
    report.type = PartitionDetectionMessageType::HEALTH_REPORT;
    report.sender_id = node_id_;
    report.target_id = message.sender_id;
    report.health_state = NodeHealthState::HEALTHY;
    report.timestamp = get_current_timestamp();
    
    send_message(message.sender_id, report);
}

void FailureDetector::update_config(const FailureDetectorConfig& config) {
    config_ = config;
    std::cout << "FailureDetector configuration updated" << std::endl;
}

FailureDetectorConfig FailureDetector::get_config() const {
    return config_;
}

void FailureDetector::print_statistics() const {
    auto stats = get_statistics();
    
    std::cout << "\n=== Failure Detector Statistics ===" << std::endl;
    std::cout << "Partitions Detected: " << stats.total_partitions_detected << std::endl;
    std::cout << "Partitions Recovered: " << stats.partitions_recovered << std::endl;
    std::cout << "Node Failures Detected: " << stats.node_failures_detected << std::endl;
    std::cout << "Nodes Recovered: " << stats.nodes_recovered << std::endl;
    std::cout << "Recovery Rate: " << (stats.partition_recovery_rate * 100) << "%" << std::endl;
    std::cout << "Detection Accuracy: " << (stats.detection_accuracy * 100) << "%" << std::endl;
    std::cout << "====================================" << std::endl;
}

PartitionInfo FailureDetector::get_current_partition() const {
    std::lock_guard<std::mutex> lock(partitions_mutex_);
    
    PartitionInfo current_partition;
    current_partition.partition_id = "current_partition";
    current_partition.state = current_partition_state_;
    
    // 添加当前节点
    current_partition.nodes.insert(node_id_);
    
    // 添加所有健康节点
    auto healthy_nodes = get_healthy_nodes();
    for (const auto& node_id : healthy_nodes) {
        current_partition.nodes.insert(node_id);
    }
    
    return current_partition;
}

void FailureDetector::handle_health_report(const PartitionDetectionMessage& message) {
    std::cout << "Received health report from " << message.sender_id 
              << " (state: " << static_cast<int>(message.health_state) << ")" << std::endl;
}