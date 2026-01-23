#include "realtime_sync.h"
#include <regex>
#include <thread>
#include <chrono>

namespace kvdb {
namespace stream {

RealtimeSyncProcessor::RealtimeSyncProcessor(const SyncConfig& config)
    : config_(config) {
    stats_.status = SyncStatus::ACTIVE;
}

void RealtimeSyncProcessor::process(const ChangeEvent& event) {
    if (stats_.status != SyncStatus::ACTIVE) {
        return;
    }
    
    if (!should_sync_key(event.key)) {
        return;
    }
    
    try {
        sync_to_targets(event);
        update_stats(true);
    } catch (const std::exception& e) {
        update_stats(false);
        handle_sync_error("", e);
    }
}

void RealtimeSyncProcessor::add_target(std::shared_ptr<SyncTarget> target) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    targets_.push_back(target);
}

void RealtimeSyncProcessor::remove_target(const std::string& target_name) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    targets_.erase(
        std::remove_if(targets_.begin(), targets_.end(),
            [&target_name](const std::shared_ptr<SyncTarget>& target) {
                return target->get_target_name() == target_name;
            }),
        targets_.end());
}

void RealtimeSyncProcessor::pause() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.status = SyncStatus::PAUSED;
}

void RealtimeSyncProcessor::resume() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.status = SyncStatus::ACTIVE;
}

SyncStats RealtimeSyncProcessor::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void RealtimeSyncProcessor::set_conflict_resolver(ConflictResolver resolver) {
    conflict_resolver_ = resolver;
}

bool RealtimeSyncProcessor::should_sync_key(const std::string& key) const {
    if (config_.source_patterns.empty()) {
        return true;
    }
    
    for (const auto& pattern : config_.source_patterns) {
        std::regex regex_pattern(pattern);
        if (std::regex_match(key, regex_pattern)) {
            return true;
        }
    }
    
    return false;
}

void RealtimeSyncProcessor::sync_to_targets(const ChangeEvent& event) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    std::vector<std::thread> sync_threads;
    
    for (auto& target : targets_) {
        if (!target->is_healthy()) {
            continue;
        }
        
        sync_threads.emplace_back([this, &event, target]() {
            size_t retry_count = 0;
            bool success = false;
            
            while (retry_count < config_.max_retry_count && !success) {
                try {
                    switch (event.type) {
                        case EventType::INSERT:
                            target->sync_insert(event.key, event.new_value);
                            break;
                        case EventType::UPDATE:
                            target->sync_update(event.key, event.old_value, event.new_value);
                            break;
                        case EventType::DELETE:
                            target->sync_delete(event.key, event.old_value);
                            break;
                        default:
                            break;
                    }
                    success = true;
                } catch (const std::exception& e) {
                    retry_count++;
                    if (retry_count < config_.max_retry_count) {
                        std::this_thread::sleep_for(config_.retry_delay);
                    } else {
                        handle_sync_error(target->get_target_name(), e);
                    }
                }
            }
        });
    }
    
    // 等待所有同步操作完成
    for (auto& thread : sync_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void RealtimeSyncProcessor::handle_sync_error(const std::string& target_name, const std::exception& e) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.error_messages.push_back("Target: " + target_name + ", Error: " + e.what());
    if (stats_.error_messages.size() > 100) {
        stats_.error_messages.erase(stats_.error_messages.begin());
    }
}

void RealtimeSyncProcessor::update_stats(bool success, bool conflict_resolved) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (success) {
        stats_.synced_operations++;
    } else {
        stats_.failed_operations++;
    }
    if (conflict_resolved) {
        stats_.conflict_resolutions++;
    }
    stats_.last_sync_time = std::chrono::system_clock::now();
}

// BidirectionalSyncManager 实现
BidirectionalSyncManager::BidirectionalSyncManager(const BidirectionalConfig& config)
    : config_(config) {
    setup_sync_processors();
}

BidirectionalSyncManager::~BidirectionalSyncManager() {
    stop();
}

void BidirectionalSyncManager::start() {
    if (is_running_.load()) {
        return;
    }
    
    is_running_ = true;
    
    // 启动本地到远程的流
    local_stream_->start();
    
    // 启动心跳线程
    heartbeat_thread_ = std::make_unique<std::thread>(&BidirectionalSyncManager::heartbeat_loop, this);
}

void BidirectionalSyncManager::stop() {
    if (!is_running_.load()) {
        return;
    }
    
    is_running_ = false;
    
    if (local_stream_) {
        local_stream_->stop();
    }
    
    if (heartbeat_thread_ && heartbeat_thread_->joinable()) {
        heartbeat_thread_->join();
    }
}

BidirectionalSyncManager::BidirectionalStats BidirectionalSyncManager::get_stats() const {
    BidirectionalStats stats;
    
    if (local_to_remote_processor_) {
        stats.local_to_remote = local_to_remote_processor_->get_stats();
    }
    
    if (remote_to_local_processor_) {
        stats.remote_to_local = remote_to_local_processor_->get_stats();
    }
    
    return stats;
}

void BidirectionalSyncManager::setup_sync_processors() {
    // 创建本地到远程的同步配置
    SyncConfig local_to_remote_config;
    local_to_remote_config.name = config_.name + "_local_to_remote";
    local_to_remote_config.source_patterns = config_.sync_patterns;
    
    local_to_remote_processor_ = std::make_shared<RealtimeSyncProcessor>(local_to_remote_config);
    
    // 创建本地流
    StreamConfig local_stream_config;
    local_stream_config.name = config_.name + "_local_stream";
    local_stream_config.event_types = {EventType::INSERT, EventType::UPDATE, EventType::DELETE};
    
    local_stream_ = ChangeStreamManager::instance().create_stream(local_stream_config);
    local_stream_->add_processor(local_to_remote_processor_);
}

void BidirectionalSyncManager::heartbeat_loop() {
    while (is_running_.load()) {
        // 发送心跳检查连接状态
        // 这里可以实现具体的心跳逻辑
        
        std::this_thread::sleep_for(config_.heartbeat_interval);
    }
}

// RemoteDBSyncTarget 实现
RemoteDBSyncTarget::RemoteDBSyncTarget(const RemoteConfig& config)
    : config_(config) {
    connect();
}

RemoteDBSyncTarget::~RemoteDBSyncTarget() {
    disconnect();
}

void RemoteDBSyncTarget::sync_insert(const std::string& key, const std::string& value) {
    if (!is_connected_.load()) {
        if (!connect()) {
            throw std::runtime_error("Failed to connect to remote database");
        }
    }
    
    std::string operation = "INSERT";
    if (!execute_sync_operation(operation, key, value)) {
        throw std::runtime_error("Failed to execute insert operation");
    }
}

void RemoteDBSyncTarget::sync_update(const std::string& key, const std::string& old_value, const std::string& new_value) {
    if (!is_connected_.load()) {
        if (!connect()) {
            throw std::runtime_error("Failed to connect to remote database");
        }
    }
    
    std::string operation = "UPDATE";
    if (!execute_sync_operation(operation, key, new_value)) {
        throw std::runtime_error("Failed to execute update operation");
    }
}

void RemoteDBSyncTarget::sync_delete(const std::string& key, const std::string& value) {
    if (!is_connected_.load()) {
        if (!connect()) {
            throw std::runtime_error("Failed to connect to remote database");
        }
    }
    
    std::string operation = "DELETE";
    if (!execute_sync_operation(operation, key, value)) {
        throw std::runtime_error("Failed to execute delete operation");
    }
}

std::string RemoteDBSyncTarget::get_target_name() const {
    return config_.host + ":" + std::to_string(config_.port) + "/" + config_.database_name;
}

bool RemoteDBSyncTarget::is_healthy() const {
    return is_connected_.load();
}

bool RemoteDBSyncTarget::connect() {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    
    // 这里实现具体的连接逻辑
    // 例如：TCP连接、HTTP连接等
    
    // 模拟连接过程
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    is_connected_ = true;
    return true;
}

void RemoteDBSyncTarget::disconnect() {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    is_connected_ = false;
}

bool RemoteDBSyncTarget::execute_sync_operation(const std::string& operation, const std::string& key, const std::string& value) {
    // 这里实现具体的同步操作
    // 例如：发送HTTP请求、TCP消息等
    
    // 模拟操作执行
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    return true;
}

} // namespace stream
} // namespace kvdb