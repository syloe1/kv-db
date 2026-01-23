#pragma once

#include "stream_types.h"
#include "change_stream.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>

namespace kvdb {
namespace stream {

// 同步目标接口
class SyncTarget {
public:
    virtual ~SyncTarget() = default;
    virtual void sync_insert(const std::string& key, const std::string& value) = 0;
    virtual void sync_update(const std::string& key, const std::string& old_value, const std::string& new_value) = 0;
    virtual void sync_delete(const std::string& key, const std::string& value) = 0;
    virtual std::string get_target_name() const = 0;
    virtual bool is_healthy() const = 0;
};

// 同步配置
struct SyncConfig {
    std::string name;
    std::vector<std::string> source_patterns;
    std::vector<std::string> target_names;
    bool enable_conflict_resolution = true;
    std::chrono::milliseconds sync_timeout{5000};
    size_t max_retry_count = 3;
    std::chrono::milliseconds retry_delay{1000};
    bool enable_batch_sync = true;
    size_t batch_size = 100;
};

// 同步状态
enum class SyncStatus {
    ACTIVE,
    PAUSED,
    ERROR,
    DISCONNECTED
};

// 同步统计
struct SyncStats {
    size_t synced_operations = 0;
    size_t failed_operations = 0;
    size_t conflict_resolutions = 0;
    std::chrono::system_clock::time_point last_sync_time;
    SyncStatus status = SyncStatus::ACTIVE;
    std::vector<std::string> error_messages;
};

// 实时同步处理器
class RealtimeSyncProcessor : public StreamProcessor {
public:
    explicit RealtimeSyncProcessor(const SyncConfig& config);
    ~RealtimeSyncProcessor() override = default;
    
    // StreamProcessor 接口
    void process(const ChangeEvent& event) override;
    std::string get_name() const override { return config_.name; }
    
    // 同步目标管理
    void add_target(std::shared_ptr<SyncTarget> target);
    void remove_target(const std::string& target_name);
    
    // 状态管理
    void pause();
    void resume();
    SyncStats get_stats() const;
    
    // 冲突解决策略
    using ConflictResolver = std::function<std::string(const std::string& key, 
                                                      const std::string& local_value,
                                                      const std::string& remote_value)>;
    void set_conflict_resolver(ConflictResolver resolver);

private:
    SyncConfig config_;
    std::vector<std::shared_ptr<SyncTarget>> targets_;
    mutable std::mutex targets_mutex_;
    
    mutable std::mutex stats_mutex_;
    SyncStats stats_;
    
    ConflictResolver conflict_resolver_;
    
    // 内部方法
    bool should_sync_key(const std::string& key) const;
    void sync_to_targets(const ChangeEvent& event);
    void handle_sync_error(const std::string& target_name, const std::exception& e);
    void update_stats(bool success, bool conflict_resolved = false);
};

// 双向同步管理器
class BidirectionalSyncManager {
public:
    struct BidirectionalConfig {
        std::string name;
        std::string local_db_name;
        std::string remote_db_name;
        std::vector<std::string> sync_patterns;
        bool enable_conflict_detection = true;
        std::chrono::milliseconds heartbeat_interval{30000};
    };
    
    explicit BidirectionalSyncManager(const BidirectionalConfig& config);
    ~BidirectionalSyncManager();
    
    void start();
    void stop();
    
    // 获取同步状态
    struct BidirectionalStats {
        SyncStats local_to_remote;
        SyncStats remote_to_local;
        size_t conflicts_detected = 0;
        std::chrono::system_clock::time_point last_heartbeat;
    };
    
    BidirectionalStats get_stats() const;

private:
    BidirectionalConfig config_;
    std::shared_ptr<RealtimeSyncProcessor> local_to_remote_processor_;
    std::shared_ptr<RealtimeSyncProcessor> remote_to_local_processor_;
    std::shared_ptr<ChangeStream> local_stream_;
    std::shared_ptr<ChangeStream> remote_stream_;
    
    std::atomic<bool> is_running_{false};
    std::unique_ptr<std::thread> heartbeat_thread_;
    
    void setup_sync_processors();
    void heartbeat_loop();
};

// 远程数据库同步目标
class RemoteDBSyncTarget : public SyncTarget {
public:
    struct RemoteConfig {
        std::string host;
        int port;
        std::string database_name;
        std::string username;
        std::string password;
        std::chrono::milliseconds connection_timeout{5000};
    };
    
    explicit RemoteDBSyncTarget(const RemoteConfig& config);
    ~RemoteDBSyncTarget() override;
    
    // SyncTarget 接口
    void sync_insert(const std::string& key, const std::string& value) override;
    void sync_update(const std::string& key, const std::string& old_value, const std::string& new_value) override;
    void sync_delete(const std::string& key, const std::string& value) override;
    std::string get_target_name() const override;
    bool is_healthy() const override;

private:
    RemoteConfig config_;
    std::atomic<bool> is_connected_{false};
    mutable std::mutex connection_mutex_;
    
    bool connect();
    void disconnect();
    bool execute_sync_operation(const std::string& operation, const std::string& key, const std::string& value);
};

} // namespace stream
} // namespace kvdb