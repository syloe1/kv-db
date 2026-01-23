#pragma once

#include "stream_types.h"
#include "event_driven.h"
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <queue>
#include <chrono>

namespace kvdb {
namespace stream {

// 流式计算操作符接口
class StreamOperator {
public:
    virtual ~StreamOperator() = default;
    virtual std::vector<ChangeEvent> process(const std::vector<ChangeEvent>& events) = 0;
    virtual std::string get_operator_name() const = 0;
};

// 窗口类型
enum class WindowType {
    TUMBLING,    // 滚动窗口
    SLIDING,     // 滑动窗口
    SESSION      // 会话窗口
};

// 窗口配置
struct WindowConfig {
    WindowType type;
    std::chrono::milliseconds size;
    std::chrono::milliseconds slide{0}; // 仅用于滑动窗口
    std::chrono::milliseconds gap{0};   // 仅用于会话窗口
    std::string key_selector; // 用于分组的键选择器
};

// 流式计算管道
class StreamPipeline {
public:
    explicit StreamPipeline(const std::string& name);
    ~StreamPipeline() = default;
    
    // 添加操作符
    StreamPipeline& map(std::function<ChangeEvent(const ChangeEvent&)> mapper);
    StreamPipeline& filter(std::function<bool(const ChangeEvent&)> predicate);
    StreamPipeline& reduce(std::function<ChangeEvent(const ChangeEvent&, const ChangeEvent&)> reducer);
    StreamPipeline& window(const WindowConfig& config);
    StreamPipeline& group_by(std::function<std::string(const ChangeEvent&)> key_selector);
    StreamPipeline& join(std::shared_ptr<StreamPipeline> other, 
                        std::function<ChangeEvent(const ChangeEvent&, const ChangeEvent&)> joiner,
                        std::chrono::milliseconds window_size);
    
    // 添加自定义操作符
    StreamPipeline& add_operator(std::shared_ptr<StreamOperator> op);
    
    // 输出到处理器
    void to(std::shared_ptr<StreamProcessor> processor);
    
    // 执行管道
    std::vector<ChangeEvent> execute(const std::vector<ChangeEvent>& input);
    
    // 获取管道名称
    std::string get_name() const { return name_; }

private:
    std::string name_;
    std::vector<std::shared_ptr<StreamOperator>> operators_;
    std::vector<std::shared_ptr<StreamProcessor>> outputs_;
};

// Map 操作符
class MapOperator : public StreamOperator {
public:
    using MapFunction = std::function<ChangeEvent(const ChangeEvent&)>;
    
    explicit MapOperator(MapFunction mapper) : mapper_(mapper) {}
    
    std::vector<ChangeEvent> process(const std::vector<ChangeEvent>& events) override {
        std::vector<ChangeEvent> result;
        for (const auto& event : events) {
            result.push_back(mapper_(event));
        }
        return result;
    }
    
    std::string get_operator_name() const override { return "Map"; }

private:
    MapFunction mapper_;
};

// Filter 操作符
class FilterOperator : public StreamOperator {
public:
    using FilterFunction = std::function<bool(const ChangeEvent&)>;
    
    explicit FilterOperator(FilterFunction predicate) : predicate_(predicate) {}
    
    std::vector<ChangeEvent> process(const std::vector<ChangeEvent>& events) override {
        std::vector<ChangeEvent> result;
        for (const auto& event : events) {
            if (predicate_(event)) {
                result.push_back(event);
            }
        }
        return result;
    }
    
    std::string get_operator_name() const override { return "Filter"; }

private:
    FilterFunction predicate_;
};

// Reduce 操作符
class ReduceOperator : public StreamOperator {
public:
    using ReduceFunction = std::function<ChangeEvent(const ChangeEvent&, const ChangeEvent&)>;
    
    explicit ReduceOperator(ReduceFunction reducer) : reducer_(reducer) {}
    
    std::vector<ChangeEvent> process(const std::vector<ChangeEvent>& events) override {
        if (events.empty()) {
            return {};
        }
        
        ChangeEvent result = events[0];
        for (size_t i = 1; i < events.size(); ++i) {
            result = reducer_(result, events[i]);
        }
        
        return {result};
    }
    
    std::string get_operator_name() const override { return "Reduce"; }

private:
    ReduceFunction reducer_;
};

// 窗口操作符
class WindowOperator : public StreamOperator {
public:
    explicit WindowOperator(const WindowConfig& config);
    
    std::vector<ChangeEvent> process(const std::vector<ChangeEvent>& events) override;
    std::string get_operator_name() const override { return "Window"; }

private:
    WindowConfig config_;
    
    struct WindowState {
        std::vector<ChangeEvent> events;
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
    };
    
    std::unordered_map<std::string, std::vector<WindowState>> windows_;
    
    std::vector<WindowState> create_tumbling_windows(const std::vector<ChangeEvent>& events);
    std::vector<WindowState> create_sliding_windows(const std::vector<ChangeEvent>& events);
    std::vector<WindowState> create_session_windows(const std::vector<ChangeEvent>& events);
    std::string get_window_key(const ChangeEvent& event);
};

// GroupBy 操作符
class GroupByOperator : public StreamOperator {
public:
    using KeySelector = std::function<std::string(const ChangeEvent&)>;
    
    explicit GroupByOperator(KeySelector key_selector) : key_selector_(key_selector) {}
    
    std::vector<ChangeEvent> process(const std::vector<ChangeEvent>& events) override {
        std::unordered_map<std::string, std::vector<ChangeEvent>> groups;
        
        for (const auto& event : events) {
            std::string key = key_selector_(event);
            groups[key].push_back(event);
        }
        
        std::vector<ChangeEvent> result;
        for (const auto& [key, group_events] : groups) {
            // 创建一个表示组的事件
            ChangeEvent group_event;
            group_event.type = EventType::UPDATE;
            group_event.key = key;
            group_event.metadata["group_size"] = std::to_string(group_events.size());
            group_event.timestamp = std::chrono::system_clock::now();
            result.push_back(group_event);
        }
        
        return result;
    }
    
    std::string get_operator_name() const override { return "GroupBy"; }

private:
    KeySelector key_selector_;
};

// Join 操作符
class JoinOperator : public StreamOperator {
public:
    using JoinFunction = std::function<ChangeEvent(const ChangeEvent&, const ChangeEvent&)>;
    
    JoinOperator(std::shared_ptr<StreamPipeline> right_stream, 
                JoinFunction joiner, 
                std::chrono::milliseconds window_size)
        : right_stream_(right_stream), joiner_(joiner), window_size_(window_size) {}
    
    std::vector<ChangeEvent> process(const std::vector<ChangeEvent>& left_events) override;
    std::string get_operator_name() const override { return "Join"; }

private:
    std::shared_ptr<StreamPipeline> right_stream_;
    JoinFunction joiner_;
    std::chrono::milliseconds window_size_;
    
    // 缓存右侧流的事件
    std::vector<ChangeEvent> right_buffer_;
    std::chrono::system_clock::time_point last_cleanup_;
    
    void cleanup_expired_events();
    bool events_match(const ChangeEvent& left, const ChangeEvent& right);
};

// 流式计算引擎
class StreamComputingEngine {
public:
    static StreamComputingEngine& instance();
    
    // 注册和管理管道
    void register_pipeline(std::shared_ptr<StreamPipeline> pipeline);
    void unregister_pipeline(const std::string& name);
    std::shared_ptr<StreamPipeline> get_pipeline(const std::string& name);
    
    // 启动和停止引擎
    void start();
    void stop();
    
    // 处理事件流
    void process_stream(const std::string& pipeline_name, const std::vector<ChangeEvent>& events);
    
    // 获取统计信息
    struct EngineStats {
        size_t registered_pipelines = 0;
        size_t events_processed = 0;
        size_t events_failed = 0;
        std::chrono::system_clock::time_point last_process_time;
        bool is_running = false;
    };
    
    EngineStats get_stats() const;

private:
    StreamComputingEngine() = default;
    
    std::unordered_map<std::string, std::shared_ptr<StreamPipeline>> pipelines_;
    mutable std::mutex pipelines_mutex_;
    
    std::atomic<bool> is_running_{false};
    
    mutable std::mutex stats_mutex_;
    EngineStats stats_;
    
    void update_stats(bool success);
};

// 流式SQL查询引擎
class StreamSQLEngine {
public:
    struct SQLQuery {
        std::string name;
        std::string sql;
        std::chrono::milliseconds window_size{5000};
        bool continuous = true;
    };
    
    static StreamSQLEngine& instance();
    
    // 注册SQL查询
    void register_query(const SQLQuery& query);
    void unregister_query(const std::string& name);
    
    // 执行查询
    std::vector<ChangeEvent> execute_query(const std::string& name, const std::vector<ChangeEvent>& events);
    
    // 获取所有查询
    std::vector<std::string> get_registered_queries() const;

private:
    StreamSQLEngine() = default;
    
    std::unordered_map<std::string, SQLQuery> queries_;
    mutable std::mutex queries_mutex_;
    
    // SQL解析和执行
    std::shared_ptr<StreamPipeline> parse_sql_to_pipeline(const std::string& sql);
    std::vector<ChangeEvent> apply_sql_operations(const std::string& sql, const std::vector<ChangeEvent>& events);
};

} // namespace stream
} // namespace kvdb