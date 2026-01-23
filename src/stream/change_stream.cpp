#include "change_stream.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <random>

namespace kvdb {
namespace stream {

std::string ChangeEvent::generate_event_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 32; ++i) {
        ss << dis(gen);
    }
    return ss.str();
}

ChangeStream::ChangeStream(const StreamConfig& config) 
    : config_(config) {
    stats_.start_time = std::chrono::system_clock::now();
    if (config_.enable_persistence) {
        load_checkpoint();
    }
}

ChangeStream::~ChangeStream() {
    stop();
}

void ChangeStream::start() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (state_.load() == StreamState::RUNNING) {
        return;
    }
    
    state_ = StreamState::STARTING;
    should_stop_ = false;
    
    worker_thread_ = std::make_unique<std::thread>(&ChangeStream::worker_loop, this);
    state_ = StreamState::RUNNING;
}

void ChangeStream::stop() {
    if (state_.load() == StreamState::STOPPED) {
        return;
    }
    
    should_stop_ = true;
    queue_cv_.notify_all();
    
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
    
    if (config_.enable_persistence) {
        save_checkpoint();
    }
    
    state_ = StreamState::STOPPED;
}

void ChangeStream::pause() {
    if (state_.load() == StreamState::RUNNING) {
        state_ = StreamState::PAUSED;
    }
}

void ChangeStream::resume() {
    if (state_.load() == StreamState::PAUSED) {
        state_ = StreamState::RUNNING;
        queue_cv_.notify_all();
    }
}

void ChangeStream::add_processor(std::shared_ptr<StreamProcessor> processor) {
    std::lock_guard<std::mutex> lock(processors_mutex_);
    processors_.push_back(processor);
}

void ChangeStream::remove_processor(const std::string& name) {
    std::lock_guard<std::mutex> lock(processors_mutex_);
    processors_.erase(
        std::remove_if(processors_.begin(), processors_.end(),
            [&name](const std::shared_ptr<StreamProcessor>& p) {
                return p->get_name() == name;
            }),
        processors_.end());
}

void ChangeStream::publish_event(const ChangeEvent& event) {
    if (state_.load() == StreamState::STOPPED) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (event_queue_.size() >= config_.buffer_size) {
        // 丢弃最老的事件
        event_queue_.pop();
    }
    
    event_queue_.push(event);
    queue_cv_.notify_one();
}

StreamStats ChangeStream::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto stats = stats_;
    stats.state = state_.load();
    return stats;
}

void ChangeStream::set_checkpoint(const std::string& checkpoint) {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    current_checkpoint_ = checkpoint;
    if (config_.enable_persistence) {
        save_checkpoint();
    }
}

std::string ChangeStream::get_checkpoint() const {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    return current_checkpoint_;
}

void ChangeStream::worker_loop() {
    while (!should_stop_) {
        if (state_.load() == StreamState::PAUSED) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        process_events();
        
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait_for(lock, config_.batch_timeout, 
            [this] { return !event_queue_.empty() || should_stop_; });
    }
}

void ChangeStream::process_events() {
    std::vector<ChangeEvent> batch;
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!event_queue_.empty() && batch.size() < 100) {
            batch.push_back(event_queue_.front());
            event_queue_.pop();
        }
    }
    
    for (const auto& event : batch) {
        bool should_process = should_process_event(event);
        bool processed = false;
        bool failed = false;
        
        if (should_process) {
            std::lock_guard<std::mutex> lock(processors_mutex_);
            for (auto& processor : processors_) {
                try {
                    processor->process(event);
                    processed = true;
                } catch (const std::exception& e) {
                    failed = true;
                    // 记录错误日志
                }
            }
        }
        
        update_stats(processed, !should_process, failed);
    }
}

bool ChangeStream::should_process_event(const ChangeEvent& event) const {
    // 检查事件类型过滤
    if (!config_.event_types.empty()) {
        bool type_match = std::find(config_.event_types.begin(), 
                                   config_.event_types.end(), 
                                   event.type) != config_.event_types.end();
        if (!type_match) {
            return false;
        }
    }
    
    // 检查键模式过滤
    if (!config_.key_patterns.empty()) {
        bool pattern_match = false;
        for (const auto& pattern : config_.key_patterns) {
            std::regex regex_pattern(pattern);
            if (std::regex_match(event.key, regex_pattern)) {
                pattern_match = true;
                break;
            }
        }
        if (!pattern_match) {
            return false;
        }
    }
    
    // 应用自定义过滤器
    if (config_.filter && !config_.filter(event)) {
        return false;
    }
    
    return true;
}

void ChangeStream::update_stats(bool processed, bool filtered, bool failed) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (processed) {
        stats_.events_processed++;
    }
    if (filtered) {
        stats_.events_filtered++;
    }
    if (failed) {
        stats_.events_failed++;
    }
    stats_.last_event_time = std::chrono::system_clock::now();
}

void ChangeStream::save_checkpoint() {
    if (config_.checkpoint_path.empty()) {
        return;
    }
    
    std::ofstream file(config_.checkpoint_path);
    if (file.is_open()) {
        file << current_checkpoint_;
        file.close();
    }
}

void ChangeStream::load_checkpoint() {
    if (config_.checkpoint_path.empty()) {
        return;
    }
    
    std::ifstream file(config_.checkpoint_path);
    if (file.is_open()) {
        std::getline(file, current_checkpoint_);
        file.close();
    }
}

// ChangeStreamManager 实现
ChangeStreamManager& ChangeStreamManager::instance() {
    static ChangeStreamManager instance;
    return instance;
}

std::shared_ptr<ChangeStream> ChangeStreamManager::create_stream(const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto stream = std::make_shared<ChangeStream>(config);
    streams_[config.name] = stream;
    return stream;
}

void ChangeStreamManager::remove_stream(const std::string& name) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(name);
    if (it != streams_.end()) {
        it->second->stop();
        streams_.erase(it);
    }
}

std::shared_ptr<ChangeStream> ChangeStreamManager::get_stream(const std::string& name) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(name);
    return (it != streams_.end()) ? it->second : nullptr;
}

void ChangeStreamManager::publish_global_event(const ChangeEvent& event) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    for (auto& [name, stream] : streams_) {
        stream->publish_event(event);
    }
}

std::vector<std::pair<std::string, StreamStats>> ChangeStreamManager::get_all_stats() const {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    std::vector<std::pair<std::string, StreamStats>> result;
    
    for (const auto& [name, stream] : streams_) {
        result.emplace_back(name, stream->get_stats());
    }
    
    return result;
}

} // namespace stream
} // namespace kvdb