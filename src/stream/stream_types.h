#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <variant>
#include <unordered_map>

namespace kvdb {
namespace stream {

// 事件类型
enum class EventType {
    INSERT,
    UPDATE,
    DELETE,
    TRANSACTION_BEGIN,
    TRANSACTION_COMMIT,
    TRANSACTION_ROLLBACK
};

// 变更事件
struct ChangeEvent {
    std::string id;
    EventType type;
    std::string key;
    std::string old_value;
    std::string new_value;
    std::chrono::system_clock::time_point timestamp;
    std::string transaction_id;
    std::unordered_map<std::string, std::string> metadata;
    
    ChangeEvent() = default;
    ChangeEvent(EventType t, const std::string& k, const std::string& old_val = "", 
                const std::string& new_val = "", const std::string& tx_id = "")
        : type(t), key(k), old_value(old_val), new_value(new_val), 
          transaction_id(tx_id), timestamp(std::chrono::system_clock::now()) {
        id = generate_event_id();
    }
    
private:
    std::string generate_event_id();
};

// 流式处理器接口
class StreamProcessor {
public:
    virtual ~StreamProcessor() = default;
    virtual void process(const ChangeEvent& event) = 0;
    virtual std::string get_name() const = 0;
};

// 事件过滤器
using EventFilter = std::function<bool(const ChangeEvent&)>;

// 流配置
struct StreamConfig {
    std::string name;
    std::vector<EventType> event_types;
    std::vector<std::string> key_patterns;
    EventFilter filter;
    size_t buffer_size = 1000;
    std::chrono::milliseconds batch_timeout{100};
    bool enable_persistence = false;
    std::string checkpoint_path;
};

// 流状态
enum class StreamState {
    STOPPED,
    STARTING,
    RUNNING,
    PAUSED,
    ERROR
};

// 流统计信息
struct StreamStats {
    size_t events_processed = 0;
    size_t events_filtered = 0;
    size_t events_failed = 0;
    std::chrono::system_clock::time_point last_event_time;
    std::chrono::system_clock::time_point start_time;
    StreamState state = StreamState::STOPPED;
};

} // namespace stream
} // namespace kvdb