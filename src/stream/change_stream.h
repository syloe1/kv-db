#pragma once

#include "stream_types.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>

namespace kvdb {
namespace stream {

// Change Stream 实现
class ChangeStream {
public:
    explicit ChangeStream(const StreamConfig& config);
    ~ChangeStream();
    
    // 启动/停止流
    void start();
    void stop();
    void pause();
    void resume();
    
    // 添加处理器
    void add_processor(std::shared_ptr<StreamProcessor> processor);
    void remove_processor(const std::string& name);
    
    // 发布事件
    void publish_event(const ChangeEvent& event);
    
    // 获取状态和统计
    StreamState get_state() const { return state_.load(); }
    StreamStats get_stats() const;
    
    // 设置检查点
    void set_checkpoint(const std::string& checkpoint);
    std::string get_checkpoint() const;

private:
    StreamConfig config_;
    std::atomic<StreamState> state_{StreamState::STOPPED};
    
    // 事件队列
    std::queue<ChangeEvent> event_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 处理器列表
    std::vector<std::shared_ptr<StreamProcessor>> processors_;
    mutable std::mutex processors_mutex_;
    
    // 工作线程
    std::unique_ptr<std::thread> worker_thread_;
    std::atomic<bool> should_stop_{false};
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    StreamStats stats_;
    
    // 检查点
    mutable std::mutex checkpoint_mutex_;
    std::string current_checkpoint_;
    
    // 内部方法
    void worker_loop();
    void process_events();
    bool should_process_event(const ChangeEvent& event) const;
    void update_stats(bool processed, bool filtered, bool failed);
    void save_checkpoint();
    void load_checkpoint();
};

// Change Stream 管理器
class ChangeStreamManager {
public:
    static ChangeStreamManager& instance();
    
    // 创建和管理流
    std::shared_ptr<ChangeStream> create_stream(const StreamConfig& config);
    void remove_stream(const std::string& name);
    std::shared_ptr<ChangeStream> get_stream(const std::string& name);
    
    // 全局事件发布
    void publish_global_event(const ChangeEvent& event);
    
    // 获取所有流的状态
    std::vector<std::pair<std::string, StreamStats>> get_all_stats() const;

private:
    ChangeStreamManager() = default;
    
    mutable std::mutex streams_mutex_;
    std::unordered_map<std::string, std::shared_ptr<ChangeStream>> streams_;
};

} // namespace stream
} // namespace kvdb