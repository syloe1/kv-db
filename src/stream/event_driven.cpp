#include "event_driven.h"
#include <algorithm>
#include <regex>
#include <thread>

namespace kvdb {
namespace stream {

// EventBus 实现
EventBus& EventBus::instance() {
    static EventBus instance;
    return instance;
}

void EventBus::register_handler(std::shared_ptr<EventHandler> handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_[handler->get_handler_name()] = handler;
}

void EventBus::unregister_handler(const std::string& handler_name) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_.erase(handler_name);
}

void EventBus::add_route(const EventRoute& route) {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    routes_.push_back(route);
    
    // 按优先级排序
    std::sort(routes_.begin(), routes_.end(),
        [](const EventRoute& a, const EventRoute& b) {
            return a.priority > b.priority;
        });
}

void EventBus::remove_route(const std::string& route_name) {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    routes_.erase(
        std::remove_if(routes_.begin(), routes_.end(),
            [&route_name](const EventRoute& route) {
                return route.name == route_name;
            }),
        routes_.end());
}

void EventBus::publish(const ChangeEvent& event) {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.events_published++;
        stats_.last_event_time = std::chrono::system_clock::now();
    }
    
    process_event(event);
}

void EventBus::publish_async(const ChangeEvent& event) {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.events_published++;
        stats_.last_event_time = std::chrono::system_clock::now();
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        event_queue_.push(event);
    }
    queue_cv_.notify_one();
}

void EventBus::start() {
    if (is_running_.load()) {
        return;
    }
    
    is_running_ = true;
    
    // 启动工作线程
    for (size_t i = 0; i < thread_count_; ++i) {
        worker_threads_.emplace_back(
            std::make_unique<std::thread>(&EventBus::worker_loop, this));
    }
}

void EventBus::stop() {
    if (!is_running_.load()) {
        return;
    }
    
    is_running_ = false;
    queue_cv_.notify_all();
    
    // 等待所有工作线程结束
    for (auto& thread : worker_threads_) {
        if (thread && thread->joinable()) {
            thread->join();
        }
    }
    worker_threads_.clear();
}

EventBus::EventBusStats EventBus::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto stats = stats_;
    
    {
        std::lock_guard<std::mutex> handlers_lock(handlers_mutex_);
        stats.active_handlers = handlers_.size();
    }
    
    {
        std::lock_guard<std::mutex> routes_lock(routes_mutex_);
        stats.active_routes = routes_.size();
    }
    
    return stats;
}

void EventBus::worker_loop() {
    while (is_running_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return !event_queue_.empty() || !is_running_.load(); });
        
        if (!is_running_.load()) {
            break;
        }
        
        if (!event_queue_.empty()) {
            auto event = event_queue_.front();
            event_queue_.pop();
            lock.unlock();
            
            process_event(event);
        }
    }
}

void EventBus::process_event(const ChangeEvent& event) {
    auto handlers = find_handlers(event);
    
    bool processed = false;
    bool failed = false;
    
    for (auto& handler : handlers) {
        try {
            handler->handle(event);
            processed = true;
        } catch (const std::exception& e) {
            failed = true;
            // 记录错误日志
        }
    }
    
    update_stats(processed, failed);
}

std::vector<std::shared_ptr<EventHandler>> EventBus::find_handlers(const ChangeEvent& event) {
    std::vector<std::shared_ptr<EventHandler>> result;
    
    std::lock_guard<std::mutex> routes_lock(routes_mutex_);
    std::lock_guard<std::mutex> handlers_lock(handlers_mutex_);
    
    for (const auto& route : routes_) {
        if (matches_route(route, event)) {
            for (const auto& handler_name : route.handler_names) {
                auto it = handlers_.find(handler_name);
                if (it != handlers_.end()) {
                    result.push_back(it->second);
                }
            }
        }
    }
    
    // 按优先级排序
    std::sort(result.begin(), result.end(),
        [](const std::shared_ptr<EventHandler>& a, const std::shared_ptr<EventHandler>& b) {
            return a->get_priority() > b->get_priority();
        });
    
    return result;
}

bool EventBus::matches_route(const EventRoute& route, const ChangeEvent& event) {
    // 检查事件类型
    if (!route.event_types.empty()) {
        bool type_match = std::find(route.event_types.begin(), route.event_types.end(), event.type) != route.event_types.end();
        if (!type_match) {
            return false;
        }
    }
    
    // 检查键模式
    if (!route.key_patterns.empty()) {
        bool pattern_match = false;
        for (const auto& pattern : route.key_patterns) {
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
    if (route.filter && !route.filter(event)) {
        return false;
    }
    
    return true;
}

void EventBus::update_stats(bool processed, bool failed) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (processed) {
        stats_.events_processed++;
    }
    if (failed) {
        stats_.events_failed++;
    }
}

// BatchEventHandler 实现
BatchEventHandler::BatchEventHandler(const std::string& name, BatchHandler handler, int priority)
    : name_(name), handler_(handler), priority_(priority) {
    
    flush_thread_ = std::make_unique<std::thread>(&BatchEventHandler::flush_loop, this);
}

BatchEventHandler::BatchEventHandler(const std::string& name, BatchHandler handler, const BatchConfig& config, int priority)
    : name_(name), handler_(handler), config_(config), priority_(priority) {
    
    flush_thread_ = std::make_unique<std::thread>(&BatchEventHandler::flush_loop, this);
}

BatchEventHandler::~BatchEventHandler() {
    should_stop_ = true;
    batch_cv_.notify_all();
    
    if (flush_thread_ && flush_thread_->joinable()) {
        flush_thread_->join();
    }
}

void BatchEventHandler::handle(const ChangeEvent& event) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    batch_.push_back(event);
    
    // 检查是否需要立即处理
    bool should_flush = false;
    if (batch_.size() >= config_.batch_size) {
        should_flush = true;
    } else if (estimate_memory_usage() >= config_.max_memory_usage) {
        should_flush = true;
    }
    
    if (should_flush) {
        batch_cv_.notify_one();
    }
}

void BatchEventHandler::flush() {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    if (!batch_.empty()) {
        process_batch();
    }
}

void BatchEventHandler::flush_loop() {
    while (!should_stop_) {
        std::unique_lock<std::mutex> lock(batch_mutex_);
        batch_cv_.wait_for(lock, config_.batch_timeout, 
            [this] { return !batch_.empty() || should_stop_; });
        
        if (should_stop_) {
            break;
        }
        
        if (!batch_.empty()) {
            process_batch();
        }
    }
}

void BatchEventHandler::process_batch() {
    if (batch_.empty()) {
        return;
    }
    
    auto batch_copy = batch_;
    batch_.clear();
    
    try {
        handler_(batch_copy);
    } catch (const std::exception& e) {
        // 记录错误日志
    }
}

size_t BatchEventHandler::estimate_memory_usage() const {
    size_t total_size = 0;
    for (const auto& event : batch_) {
        total_size += event.key.size() + event.old_value.size() + event.new_value.size();
        total_size += sizeof(ChangeEvent);
    }
    return total_size;
}

// EventAggregator 实现
EventAggregator::EventAggregator(const std::vector<AggregationRule>& rules)
    : rules_(rules) {}

void EventAggregator::add_event(const ChangeEvent& event) {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    for (const auto& rule : rules_) {
        if (matches_rule(rule, event)) {
            std::string window_key = get_window_key(rule, event);
            
            auto& window = windows_[window_key];
            if (window.events.empty()) {
                window.window_start = std::chrono::system_clock::now();
            }
            
            window.events.push_back(event);
        }
    }
    
    cleanup_expired_windows();
}

std::vector<ChangeEvent> EventAggregator::get_aggregated_events() {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    std::vector<ChangeEvent> result;
    
    auto now = std::chrono::system_clock::now();
    
    for (auto it = windows_.begin(); it != windows_.end();) {
        auto& [window_key, window_data] = *it;
        
        // 找到对应的规则
        AggregationRule* matching_rule = nullptr;
        for (auto& rule : rules_) {
            if (window_key.find(rule.name) == 0) {
                matching_rule = &rule;
                break;
            }
        }
        
        if (matching_rule && 
            (now - window_data.window_start) >= matching_rule->window_size) {
            
            if (!window_data.events.empty()) {
                auto aggregated = matching_rule->aggregator(window_data.events);
                result.push_back(aggregated);
            }
            
            it = windows_.erase(it);
        } else {
            ++it;
        }
    }
    
    return result;
}

std::string EventAggregator::get_window_key(const AggregationRule& rule, const ChangeEvent& event) {
    return rule.name + "_" + event.key;
}

bool EventAggregator::matches_rule(const AggregationRule& rule, const ChangeEvent& event) {
    if (rule.key_patterns.empty()) {
        return true;
    }
    
    for (const auto& pattern : rule.key_patterns) {
        std::regex regex_pattern(pattern);
        if (std::regex_match(event.key, regex_pattern)) {
            return true;
        }
    }
    
    return false;
}

void EventAggregator::cleanup_expired_windows() {
    auto now = std::chrono::system_clock::now();
    
    for (auto it = windows_.begin(); it != windows_.end();) {
        auto& [window_key, window_data] = *it;
        
        // 找到对应的规则
        AggregationRule* matching_rule = nullptr;
        for (auto& rule : rules_) {
            if (window_key.find(rule.name) == 0) {
                matching_rule = &rule;
                break;
            }
        }
        
        if (matching_rule && 
            (now - window_data.window_start) >= (matching_rule->window_size * 2)) {
            it = windows_.erase(it);
        } else {
            ++it;
        }
    }
}

// EventReplayer 实现
EventReplayer::EventReplayer(const ReplayConfig& config)
    : config_(config) {}

void EventReplayer::start_replay(std::shared_ptr<EventHandler> handler) {
    if (is_running_.load()) {
        return;
    }
    
    is_running_ = true;
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.is_running = true;
        stats_.replay_start_time = std::chrono::system_clock::now();
    }
    
    replay_thread_ = std::make_unique<std::thread>(&EventReplayer::replay_loop, this, handler);
}

void EventReplayer::stop_replay() {
    if (!is_running_.load()) {
        return;
    }
    
    is_running_ = false;
    
    if (replay_thread_ && replay_thread_->joinable()) {
        replay_thread_->join();
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.is_running = false;
    }
}

EventReplayer::ReplayStats EventReplayer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void EventReplayer::replay_loop(std::shared_ptr<EventHandler> handler) {
    auto events = load_events_from_checkpoint(config_.checkpoint_start, config_.checkpoint_end);
    
    auto last_event_time = std::chrono::system_clock::now();
    
    for (const auto& event : events) {
        if (!is_running_.load()) {
            break;
        }
        
        // 应用过滤器
        if (config_.filter && !config_.filter(event)) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.events_skipped++;
            continue;
        }
        
        // 保持时序
        if (config_.preserve_timing && config_.replay_speed.count() > 0) {
            auto time_diff = event.timestamp - last_event_time;
            if (time_diff.count() > 0) {
                std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::milliseconds>(time_diff));
            }
        } else if (config_.replay_speed.count() > 0) {
            std::this_thread::sleep_for(config_.replay_speed);
        }
        
        try {
            handler->handle(event);
            
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.events_replayed++;
            stats_.current_event_time = event.timestamp;
        } catch (const std::exception& e) {
            // 记录错误日志
        }
        
        last_event_time = event.timestamp;
    }
}

std::vector<ChangeEvent> EventReplayer::load_events_from_checkpoint(const std::string& start, const std::string& end) {
    // 这里实现从检查点加载事件的逻辑
    // 可以从WAL、事件存储等地方加载
    
    std::vector<ChangeEvent> events;
    // 模拟加载过程
    
    return events;
}

} // namespace stream
} // namespace kvdb