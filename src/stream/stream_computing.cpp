#include "stream_computing.h"
#include <algorithm>
#include <sstream>
#include <regex>

namespace kvdb {
namespace stream {

// StreamPipeline 实现
StreamPipeline::StreamPipeline(const std::string& name) : name_(name) {}

StreamPipeline& StreamPipeline::map(std::function<ChangeEvent(const ChangeEvent&)> mapper) {
    operators_.push_back(std::make_shared<MapOperator>(mapper));
    return *this;
}

StreamPipeline& StreamPipeline::filter(std::function<bool(const ChangeEvent&)> predicate) {
    operators_.push_back(std::make_shared<FilterOperator>(predicate));
    return *this;
}

StreamPipeline& StreamPipeline::reduce(std::function<ChangeEvent(const ChangeEvent&, const ChangeEvent&)> reducer) {
    operators_.push_back(std::make_shared<ReduceOperator>(reducer));
    return *this;
}

StreamPipeline& StreamPipeline::window(const WindowConfig& config) {
    operators_.push_back(std::make_shared<WindowOperator>(config));
    return *this;
}

StreamPipeline& StreamPipeline::group_by(std::function<std::string(const ChangeEvent&)> key_selector) {
    operators_.push_back(std::make_shared<GroupByOperator>(key_selector));
    return *this;
}

StreamPipeline& StreamPipeline::join(std::shared_ptr<StreamPipeline> other, 
                                    std::function<ChangeEvent(const ChangeEvent&, const ChangeEvent&)> joiner,
                                    std::chrono::milliseconds window_size) {
    operators_.push_back(std::make_shared<JoinOperator>(other, joiner, window_size));
    return *this;
}

StreamPipeline& StreamPipeline::add_operator(std::shared_ptr<StreamOperator> op) {
    operators_.push_back(op);
    return *this;
}

void StreamPipeline::to(std::shared_ptr<StreamProcessor> processor) {
    outputs_.push_back(processor);
}

std::vector<ChangeEvent> StreamPipeline::execute(const std::vector<ChangeEvent>& input) {
    std::vector<ChangeEvent> current = input;
    
    // 依次执行所有操作符
    for (auto& op : operators_) {
        current = op->process(current);
    }
    
    // 发送到输出处理器
    for (auto& output : outputs_) {
        for (const auto& event : current) {
            output->process(event);
        }
    }
    
    return current;
}

// WindowOperator 实现
WindowOperator::WindowOperator(const WindowConfig& config) : config_(config) {}

std::vector<ChangeEvent> WindowOperator::process(const std::vector<ChangeEvent>& events) {
    std::vector<WindowState> window_states;
    
    switch (config_.type) {
        case WindowType::TUMBLING:
            window_states = create_tumbling_windows(events);
            break;
        case WindowType::SLIDING:
            window_states = create_sliding_windows(events);
            break;
        case WindowType::SESSION:
            window_states = create_session_windows(events);
            break;
    }
    
    std::vector<ChangeEvent> result;
    for (const auto& window : window_states) {
        if (!window.events.empty()) {
            // 创建窗口聚合事件
            ChangeEvent window_event;
            window_event.type = EventType::UPDATE;
            window_event.key = "window_" + std::to_string(window.start_time.time_since_epoch().count());
            window_event.metadata["window_size"] = std::to_string(window.events.size());
            window_event.metadata["window_start"] = std::to_string(window.start_time.time_since_epoch().count());
            window_event.metadata["window_end"] = std::to_string(window.end_time.time_since_epoch().count());
            window_event.timestamp = window.end_time;
            result.push_back(window_event);
        }
    }
    
    return result;
}

std::vector<WindowOperator::WindowState> WindowOperator::create_tumbling_windows(const std::vector<ChangeEvent>& events) {
    std::vector<WindowState> windows;
    
    if (events.empty()) {
        return windows;
    }
    
    // 按时间排序
    auto sorted_events = events;
    std::sort(sorted_events.begin(), sorted_events.end(),
        [](const ChangeEvent& a, const ChangeEvent& b) {
            return a.timestamp < b.timestamp;
        });
    
    auto window_start = sorted_events[0].timestamp;
    WindowState current_window;
    current_window.start_time = window_start;
    current_window.end_time = window_start + config_.size;
    
    for (const auto& event : sorted_events) {
        if (event.timestamp >= current_window.end_time) {
            // 开始新窗口
            if (!current_window.events.empty()) {
                windows.push_back(current_window);
            }
            
            current_window = WindowState();
            current_window.start_time = event.timestamp;
            current_window.end_time = event.timestamp + config_.size;
        }
        
        current_window.events.push_back(event);
    }
    
    // 添加最后一个窗口
    if (!current_window.events.empty()) {
        windows.push_back(current_window);
    }
    
    return windows;
}

std::vector<WindowOperator::WindowState> WindowOperator::create_sliding_windows(const std::vector<ChangeEvent>& events) {
    std::vector<WindowState> windows;
    
    if (events.empty()) {
        return windows;
    }
    
    auto sorted_events = events;
    std::sort(sorted_events.begin(), sorted_events.end(),
        [](const ChangeEvent& a, const ChangeEvent& b) {
            return a.timestamp < b.timestamp;
        });
    
    auto slide_interval = config_.slide.count() > 0 ? config_.slide : config_.size;
    auto start_time = sorted_events[0].timestamp;
    
    while (start_time <= sorted_events.back().timestamp) {
        WindowState window;
        window.start_time = start_time;
        window.end_time = start_time + config_.size;
        
        for (const auto& event : sorted_events) {
            if (event.timestamp >= window.start_time && event.timestamp < window.end_time) {
                window.events.push_back(event);
            }
        }
        
        if (!window.events.empty()) {
            windows.push_back(window);
        }
        
        start_time += slide_interval;
    }
    
    return windows;
}

std::vector<WindowOperator::WindowState> WindowOperator::create_session_windows(const std::vector<ChangeEvent>& events) {
    std::vector<WindowState> windows;
    
    if (events.empty()) {
        return windows;
    }
    
    // 按键和时间分组
    std::unordered_map<std::string, std::vector<ChangeEvent>> key_groups;
    for (const auto& event : events) {
        std::string key = get_window_key(event);
        key_groups[key].push_back(event);
    }
    
    for (auto& [key, key_events] : key_groups) {
        std::sort(key_events.begin(), key_events.end(),
            [](const ChangeEvent& a, const ChangeEvent& b) {
                return a.timestamp < b.timestamp;
            });
        
        WindowState current_window;
        current_window.start_time = key_events[0].timestamp;
        current_window.end_time = key_events[0].timestamp;
        current_window.events.push_back(key_events[0]);
        
        for (size_t i = 1; i < key_events.size(); ++i) {
            auto time_gap = key_events[i].timestamp - current_window.end_time;
            
            if (time_gap > config_.gap) {
                // 开始新会话
                windows.push_back(current_window);
                
                current_window = WindowState();
                current_window.start_time = key_events[i].timestamp;
            }
            
            current_window.end_time = key_events[i].timestamp;
            current_window.events.push_back(key_events[i]);
        }
        
        windows.push_back(current_window);
    }
    
    return windows;
}

std::string WindowOperator::get_window_key(const ChangeEvent& event) {
    if (config_.key_selector.empty()) {
        return "default";
    }
    
    // 简单的键选择器实现
    if (config_.key_selector == "key") {
        return event.key;
    } else if (config_.key_selector == "transaction_id") {
        return event.transaction_id;
    }
    
    return "default";
}

// JoinOperator 实现
std::vector<ChangeEvent> JoinOperator::process(const std::vector<ChangeEvent>& left_events) {
    cleanup_expired_events();
    
    std::vector<ChangeEvent> result;
    
    for (const auto& left_event : left_events) {
        for (const auto& right_event : right_buffer_) {
            if (events_match(left_event, right_event)) {
                try {
                    ChangeEvent joined = joiner_(left_event, right_event);
                    result.push_back(joined);
                } catch (const std::exception& e) {
                    // 记录错误日志
                }
            }
        }
    }
    
    return result;
}

void JoinOperator::cleanup_expired_events() {
    auto now = std::chrono::system_clock::now();
    
    if (now - last_cleanup_ < std::chrono::seconds(1)) {
        return; // 不需要频繁清理
    }
    
    right_buffer_.erase(
        std::remove_if(right_buffer_.begin(), right_buffer_.end(),
            [this, now](const ChangeEvent& event) {
                return (now - event.timestamp) > window_size_;
            }),
        right_buffer_.end());
    
    last_cleanup_ = now;
}

bool JoinOperator::events_match(const ChangeEvent& left, const ChangeEvent& right) {
    // 简单的匹配逻辑：相同的键
    return left.key == right.key;
}

// StreamComputingEngine 实现
StreamComputingEngine& StreamComputingEngine::instance() {
    static StreamComputingEngine instance;
    return instance;
}

void StreamComputingEngine::register_pipeline(std::shared_ptr<StreamPipeline> pipeline) {
    std::lock_guard<std::mutex> lock(pipelines_mutex_);
    pipelines_[pipeline->get_name()] = pipeline;
    
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.registered_pipelines = pipelines_.size();
}

void StreamComputingEngine::unregister_pipeline(const std::string& name) {
    std::lock_guard<std::mutex> lock(pipelines_mutex_);
    pipelines_.erase(name);
    
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.registered_pipelines = pipelines_.size();
}

std::shared_ptr<StreamPipeline> StreamComputingEngine::get_pipeline(const std::string& name) {
    std::lock_guard<std::mutex> lock(pipelines_mutex_);
    auto it = pipelines_.find(name);
    return (it != pipelines_.end()) ? it->second : nullptr;
}

void StreamComputingEngine::start() {
    is_running_ = true;
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.is_running = true;
}

void StreamComputingEngine::stop() {
    is_running_ = false;
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.is_running = false;
}

void StreamComputingEngine::process_stream(const std::string& pipeline_name, const std::vector<ChangeEvent>& events) {
    if (!is_running_.load()) {
        return;
    }
    
    auto pipeline = get_pipeline(pipeline_name);
    if (!pipeline) {
        update_stats(false);
        return;
    }
    
    try {
        pipeline->execute(events);
        update_stats(true);
    } catch (const std::exception& e) {
        update_stats(false);
    }
}

StreamComputingEngine::EngineStats StreamComputingEngine::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void StreamComputingEngine::update_stats(bool success) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (success) {
        stats_.events_processed++;
    } else {
        stats_.events_failed++;
    }
    stats_.last_process_time = std::chrono::system_clock::now();
}

// StreamSQLEngine 实现
StreamSQLEngine& StreamSQLEngine::instance() {
    static StreamSQLEngine instance;
    return instance;
}

void StreamSQLEngine::register_query(const SQLQuery& query) {
    std::lock_guard<std::mutex> lock(queries_mutex_);
    queries_[query.name] = query;
}

void StreamSQLEngine::unregister_query(const std::string& name) {
    std::lock_guard<std::mutex> lock(queries_mutex_);
    queries_.erase(name);
}

std::vector<ChangeEvent> StreamSQLEngine::execute_query(const std::string& name, const std::vector<ChangeEvent>& events) {
    std::lock_guard<std::mutex> lock(queries_mutex_);
    auto it = queries_.find(name);
    if (it == queries_.end()) {
        return {};
    }
    
    return apply_sql_operations(it->second.sql, events);
}

std::vector<std::string> StreamSQLEngine::get_registered_queries() const {
    std::lock_guard<std::mutex> lock(queries_mutex_);
    std::vector<std::string> result;
    for (const auto& [name, query] : queries_) {
        result.push_back(name);
    }
    return result;
}

std::shared_ptr<StreamPipeline> StreamSQLEngine::parse_sql_to_pipeline(const std::string& sql) {
    // 简化的SQL解析实现
    auto pipeline = std::make_shared<StreamPipeline>("sql_pipeline");
    
    // 解析SELECT语句
    std::regex select_regex(R"(SELECT\s+(.+?)\s+FROM\s+(\w+))", std::regex_constants::icase);
    std::smatch match;
    
    if (std::regex_search(sql, match, select_regex)) {
        std::string columns = match[1].str();
        std::string table = match[2].str();
        
        // 添加基本的过滤和映射操作
        if (columns != "*") {
            // 实现列选择逻辑
        }
    }
    
    // 解析WHERE子句
    std::regex where_regex(R"(WHERE\s+(.+?)(?:\s+GROUP\s+BY|\s+ORDER\s+BY|$))", std::regex_constants::icase);
    if (std::regex_search(sql, match, where_regex)) {
        std::string condition = match[1].str();
        // 实现条件过滤逻辑
        pipeline->filter([condition](const ChangeEvent& event) {
            // 简化的条件评估
            return true;
        });
    }
    
    return pipeline;
}

std::vector<ChangeEvent> StreamSQLEngine::apply_sql_operations(const std::string& sql, const std::vector<ChangeEvent>& events) {
    auto pipeline = parse_sql_to_pipeline(sql);
    return pipeline->execute(events);
}

} // namespace stream
} // namespace kvdb