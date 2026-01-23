#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <mutex>
#include <functional>
#include "integrity_checker.h"

// 检查点状态
enum class CheckpointStatus {
    CREATING,
    COMPLETED,
    FAILED,
    CORRUPTED,
    EXPIRED
};

// 检查点触发条件
enum class CheckpointTrigger {
    MANUAL,           // 手动触发
    TIME_INTERVAL,    // 时间间隔
    TRANSACTION_COUNT,// 事务数量
    WAL_SIZE,        // WAL大小
    SYSTEM_SHUTDOWN  // 系统关闭
};

// 检查点信息
struct CheckpointInfo {
    std::string checkpoint_id;
    uint64_t lsn;
    uint64_t creation_time;
    uint64_t file_size;
    uint32_t file_crc32;
    std::string file_path;
    CheckpointStatus status;
    CheckpointTrigger trigger;
    std::string description;
    
    CheckpointInfo() : lsn(0), creation_time(0), file_size(0), 
                      file_crc32(0), status(CheckpointStatus::CREATING),
                      trigger(CheckpointTrigger::MANUAL) {}
};

// 检查点配置
struct CheckpointConfig {
    bool auto_checkpoint_enabled;
    std::chrono::minutes time_interval;
    uint64_t transaction_count_threshold;
    uint64_t wal_size_threshold_mb;
    size_t max_checkpoints_to_keep;
    std::string checkpoint_directory;
    bool compress_checkpoints;
    
    CheckpointConfig() : auto_checkpoint_enabled(true),
                        time_interval(30), // 30分钟
                        transaction_count_threshold(10000),
                        wal_size_threshold_mb(100),
                        max_checkpoints_to_keep(10),
                        checkpoint_directory("checkpoints"),
                        compress_checkpoints(false) {}
};

// 检查点结果
struct CheckpointResult {
    bool success;
    std::string checkpoint_id;
    uint64_t lsn;
    std::string file_path;
    uint64_t file_size;
    std::chrono::milliseconds creation_time;
    std::vector<std::string> error_messages;
    
    CheckpointResult() : success(false), lsn(0), file_size(0), creation_time(0) {}
};

// 数据库状态快照接口
class DatabaseSnapshot {
public:
    virtual ~DatabaseSnapshot() = default;
    virtual bool capture_state(const std::string& checkpoint_path) = 0;
    virtual bool restore_state(const std::string& checkpoint_path) = 0;
    virtual uint64_t get_current_lsn() const = 0;
    virtual std::vector<std::string> get_data_files() const = 0;
};

class CheckpointManager {
public:
    CheckpointManager(const std::string& checkpoint_directory);
    ~CheckpointManager();
    
    // 检查点操作
    CheckpointResult create_checkpoint();
    CheckpointResult create_checkpoint(CheckpointTrigger trigger);
    CheckpointResult create_checkpoint(CheckpointTrigger trigger, const std::string& description);
    
    // 检查点管理
    std::vector<CheckpointInfo> list_checkpoints() const;
    CheckpointInfo get_checkpoint_info(const std::string& checkpoint_id) const;
    bool restore_from_checkpoint(const std::string& checkpoint_id);
    bool delete_checkpoint(const std::string& checkpoint_id);
    void cleanup_old_checkpoints(size_t max_checkpoints);
    void cleanup_expired_checkpoints();
    
    // 自动检查点配置
    void configure_auto_checkpoint(const CheckpointConfig& config);
    void start_auto_checkpoint();
    void stop_auto_checkpoint();
    bool is_auto_checkpoint_running() const;
    
    // 验证和修复
    bool validate_checkpoint(const std::string& checkpoint_id);
    std::vector<std::string> find_corrupted_checkpoints();
    bool repair_checkpoint(const std::string& checkpoint_id);
    
    // 数据库快照接口
    void set_database_snapshot(std::shared_ptr<DatabaseSnapshot> snapshot);
    
    // 状态查询
    bool is_checkpoint_in_progress() const;
    std::string get_current_checkpoint_id() const;
    CheckpointConfig get_current_config() const;

private:
    std::string checkpoint_directory_;
    CheckpointConfig config_;
    std::shared_ptr<DatabaseSnapshot> database_snapshot_;
    std::unique_ptr<IntegrityChecker> integrity_checker_;
    
    // 状态管理
    mutable std::mutex checkpoint_mutex_;
    bool checkpoint_in_progress_;
    bool auto_checkpoint_running_;
    std::string current_checkpoint_id_;
    std::thread auto_checkpoint_thread_;
    
    // 检查点存储
    std::vector<CheckpointInfo> checkpoints_;
    
    // 私有方法
    std::string generate_checkpoint_id() const;
    std::string get_checkpoint_file_path(const std::string& checkpoint_id) const;
    std::string get_checkpoint_meta_path(const std::string& checkpoint_id) const;
    
    bool create_checkpoint_internal(CheckpointTrigger trigger, const std::string& description,
                                   CheckpointResult& result);
    bool save_checkpoint_metadata(const CheckpointInfo& info);
    bool load_checkpoint_metadata(const std::string& checkpoint_id, CheckpointInfo& info);
    bool load_all_checkpoints();
    
    void auto_checkpoint_worker();
    bool should_create_checkpoint() const;
    CheckpointTrigger determine_checkpoint_trigger() const;
    
    bool copy_database_files(const std::string& checkpoint_path);
    bool calculate_checkpoint_checksum(const std::string& checkpoint_path, uint32_t& checksum);
    bool verify_checkpoint_integrity(const CheckpointInfo& info);
    
    void cleanup_checkpoint_files(const std::string& checkpoint_id);
    uint64_t get_directory_size(const std::string& directory) const;
};