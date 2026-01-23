#pragma once

#include "stream_types.h"
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace kvdb {
namespace stream {

// 事件处理器接口
class EventHandler {
public:
    virtual ~EventHandler() = default;
    virtual void handle(const ChangeEvent& event) = 0;
    virtual std::string get_handler_name() const = 0;
    virtual int get_priority() const { return 0; } // 优先级，数字越大优先级越高
};

// 事件路由规则
struct EventRoute {
    std::string name;
    std::vector<EventType> event_types;
    std::vector<std::string> key_patterns;
    EventFilter filter;
    std::vector<std::string> handler_names;
    int priority = 0;
    bool async = true;
};

// 事件总线
class EventBus {
public:
    static EventBus& instance();
    
    // 注册和注销处理器
    void register_handler(std::shared_ptr<EventHandler> handler);
    void unregister_handler(const std::string& handler_name);
    
    // 添加和移除路由规则
    void add_route(const EventRoute& route);
    void remove_route(const std::string& route_name);
    
    // 发布事件
    void publish(const ChangeEvent& event);
    void publish_async(const ChangeEvent& event);
    
    // 启动和停止事件总线
    void start();
    void stop();
    
    // 获取统计信息
    struct EventBusStats {
        size_t events_published = 0;
        size_t events_processed = 0;
        size_t events_failed = 0;
        size_t active_handlers = 0;
        size_t active_routes = 0;
        std::chrono::system_clock::time_point last_event_time;
    };
    
    EventBusStats get_stats() const;

private:
    EventBus() = default;
    
    // 处理器管理
    std::unordered_map<std::string, std::shared_ptr<EventHandler>> handlers_;
    mutable std::mutex handlers_mutex_;
    
    // 路由规则
    std::vector<EventRoute> routes_;
    mutable std::mutex routes_mutex_;
    
    // 异步处理队列
    std::queue<ChangeEvent> event_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 工作线程
    std::vector<std::unique_ptr<std::thread>> worker_threads_;
    std::atomic<bool> is_running_{false};
    size_t thread_count_ = 4;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    EventBusStats stats_;
    
    // 内部方法
    void worker_loop();
    void process_event(const ChangeEvent& event);
    std::vector<std::shared_ptr<EventHandler>> find_handlers(const ChangeEvent& event);
    bool matches_route(const EventRoute& route, const ChangeEvent& event);
    void update_stats(bool processed, bool failed);
};

// 函数式事件处理器
class FunctionEventHandler : public EventHandler {
public:
    using HandlerFunction = std::function<void(const ChangeEvent&)>;
    
    FunctionEventHandler(const std::string& name, HandlerFunction handler, int priority = 0)
        : name_(name), handler_(handler), priority_(priority) {}
    
    void handle(const ChangeEvent& event) override {
        handler_(event);
    }
    
    std::string get_handler_name() const override {
        return name_;
    }
    
    int get_priority() const override {
        return priority_;
    }

private:
    std::string name_;
    HandlerFunction handler_;
    int priority_;
};

// 条件事件处理器
class ConditionalEventHandler : public EventHandler {
public:
    using Condition = std::function<bool(const ChangeEvent&)>;
    using Handler = std::function<void(const ChangeEvent&)>;
    
    ConditionalEventHandler(const std::string& name, Condition condition, Handler handler, int priority = 0)
        : name_(name), condition_(condition), handler_(handler), priority_(priority) {}
    
    void handle(const ChangeEvent& event) override {
        if (condition_(event)) {
            handler_(event);
        }
    }
    
    std::string get_handler_name() const override {
        return name_;
    }
    
    int get_priority() const override {
        return priority_;
    }

private:
    std::string name_;
    Condition condition_;
    Handler handler_;
    int priority_;
};

// 批处理事件处理器
class BatchEventHandler : public EventHandler {
public:
    using BatchHandler = std::function<void(const std::vector<ChangeEvent>&)>;
    
    struct BatchConfig {
        size_t batch_size = 100;
        std::chrono::milliseconds batch_timeout{1000};
        size_t max_memory_usage = 10 * 1024 * 1024; // 10MB
    };
    
    BatchEventHandler(const std::string& name, BatchHandler handler, int priority = 0);
    BatchEventHandler(const std::string& name, BatchHandler handler, const BatchConfig& config, int priority = 0);
    ~BatchEventHandler();
    
    void handle(const ChangeEvent& event) override;
    std::string get_handler_name() const override { return name_; }
    int get_priority() const override { return priority_; }
    
    void flush(); // 强制处理当前批次

private:
    std::string name_;
    BatchHandler handler_;
    BatchConfig config_;
    int priority_;
    
    std::vector<ChangeEvent> batch_;
    std::mutex batch_mutex_;
    std::condition_variable batch_cv_;
    
    std::unique_ptr<std::thread> flush_thread_;
    std::atomic<bool> should_stop_{false};
    
    void flush_loop();
    void process_batch();
    size_t estimate_memory_usage() const;
};

// 事件聚合器
class EventAggregator {
public:
    struct AggregationRule {
        std::string name;
        std::vector<std::string> key_patterns;
        std::chrono::milliseconds window_size{5000};
        std::function<ChangeEvent(const std::vector<ChangeEvent>&)> aggregator;
    };
    
    explicit EventAggregator(const std::vector<AggregationRule>& rules);
    
    void add_event(const ChangeEvent& event);
    std::vector<ChangeEvent> get_aggregated_events();

private:
    std::vector<AggregationRule> rules_;
    
    struct WindowData {
        std::vector<ChangeEvent> events;
        std::chrono::system_clock::time_point window_start;
    };
    
    std::unordered_map<std::string, WindowData> windows_;
    mutable std::mutex windows_mutex_;
    
    std::string get_window_key(const AggregationRule& rule, const ChangeEvent& event);
    bool matches_rule(const AggregationRule& rule, const ChangeEvent& event);
    void cleanup_expired_windows();
};

// 事件重放器
class EventReplayer {
public:
    struct ReplayConfig {
        std::string checkpoint_start;
        std::string checkpoint_end;
        std::chrono::milliseconds replay_speed{0}; // 0表示最快速度
        bool preserve_timing = false;
        EventFilter filter;
    };
    
    explicit EventReplayer(const ReplayConfig& config);
    
    void start_replay(std::shared_ptr<EventHandler> handler);
    void stop_replay();
    
    struct ReplayStats {
        size_t events_replayed = 0;
        size_t events_skipped = 0;
        std::chrono::system_clock::time_point replay_start_time;
        std::chrono::system_clock::time_point current_event_time;
        bool is_running = false;
    };
    
    ReplayStats get_stats() const;

private:
    ReplayConfig config_;
    std::atomic<bool> is_running_{false};
    std::unique_ptr<std::thread> replay_thread_;
    
    mutable std::mutex stats_mutex_;
    ReplayStats stats_;
    
    void replay_loop(std::shared_ptr<EventHandler> handler);
    std::vector<ChangeEvent> load_events_from_checkpoint(const std::string& start, const std::string& end);
};

} // namespace stream
} // namespace kvdb