#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <future>
#include "segmented_wal.h"
#include "integrity_checker.h"

// 恢复结果状态
enum class RecoveryStatus {
    SUCCESS,
    PARTIAL_SUCCESS,
    FAILED,
    CORRUPTED_DATA,
    INSUFFICIENT_SPACE,
    PERMISSION_DENIED
};

// 恢复错误类型
enum class RecoveryError {
    CHECKPOINT_CORRUPTED,
    WAL_SEGMENT_MISSING,
    BACKUP_CHAIN_BROKEN,
    INSUFFICIENT_SPACE,
    PERMISSION_DENIED,
    UNKNOWN_ERROR
};

// 恢复结果
struct RecoveryResult {
    RecoveryStatus status;
    uint64_t recovered_entries;
    uint64_t corrupted_entries;
    uint64_t start_lsn;
    uint64_t end_lsn;
    std::vector<std::string> error_messages;
    std::chrono::milliseconds recovery_time;
    
    RecoveryResult() : status(RecoveryStatus::FAILED), recovered_entries(0), 
                      corrupted_entries(0), start_lsn(0), end_lsn(0), 
                      recovery_time(0) {}
};

// 恢复报告
struct RecoveryReport {
    std::string recovery_id;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    RecoveryResult result;
    std::vector<std::string> processed_segments;
    std::vector<std::string> corrupted_segments;
    double integrity_rate;
    
    RecoveryReport() : integrity_rate(0.0) {}
};

// 恢复上下文
struct RecoveryContext {
    std::string recovery_id;
    uint64_t target_lsn;
    std::string checkpoint_path;
    std::string backup_path;
    bool parallel_enabled;
    size_t max_parallel_segments;
    std::function<void(const std::string&, const std::string&)> on_put;
    std::function<void(const std::string&)> on_delete;
    
    RecoveryContext() : target_lsn(0), parallel_enabled(true), max_parallel_segments(4) {}
};

class RecoveryManager {
public:
    RecoveryManager(const std::string& wal_directory);
    ~RecoveryManager();
    
    // 主要恢复操作
    RecoveryResult recover_from_crash(uint64_t checkpoint_lsn = 0);
    RecoveryResult recover_from_backup(const std::string& backup_path);
    RecoveryResult recover_to_lsn(uint64_t target_lsn);
    
    // 并行恢复
    RecoveryResult parallel_wal_recovery(const std::vector<WALSegment*>& segments);
    RecoveryResult parallel_wal_recovery(const std::vector<WALSegment*>& segments, 
                                       const RecoveryContext& context);
    
    // 验证和报告
    bool verify_recovery_integrity();
    RecoveryReport generate_recovery_report() const;
    void print_recovery_report(const RecoveryReport& report) const;
    
    // 错误处理
    void handle_recovery_failure(const RecoveryError& error);
    std::string get_error_description(RecoveryError error) const;
    
    // 配置
    void set_parallel_recovery_enabled(bool enabled);
    void set_max_parallel_segments(size_t max_segments);
    void set_recovery_callbacks(
        const std::function<void(const std::string&, const std::string&)>& on_put,
        const std::function<void(const std::string&)>& on_delete
    );
    
    // 状态查询
    bool is_recovery_in_progress() const;
    RecoveryStatus get_last_recovery_status() const;
    std::string get_current_recovery_id() const;

private:
    std::string wal_directory_;
    std::unique_ptr<SegmentedWAL> wal_;
    std::unique_ptr<IntegrityChecker> integrity_checker_;
    
    // 恢复状态
    mutable std::mutex recovery_mutex_;
    bool recovery_in_progress_;
    std::string current_recovery_id_;
    RecoveryResult last_recovery_result_;
    RecoveryReport last_recovery_report_;
    
    // 配置
    bool parallel_recovery_enabled_;
    size_t max_parallel_segments_;
    std::function<void(const std::string&, const std::string&)> on_put_callback_;
    std::function<void(const std::string&)> on_delete_callback_;
    
    // 私有方法
    std::string generate_recovery_id() const;
    RecoveryResult recover_from_segments(const std::vector<WALSegment*>& segments,
                                       const RecoveryContext& context);
    RecoveryResult recover_segment_sequential(WALSegment* segment, 
                                            const RecoveryContext& context);
    std::future<RecoveryResult> recover_segment_async(WALSegment* segment,
                                                    const RecoveryContext& context);
    
    void apply_wal_entry(const WALEntry& entry, const RecoveryContext& context);
    bool validate_recovery_prerequisites(const RecoveryContext& context);
    void update_recovery_progress(const std::string& message);
    void cleanup_recovery_resources();
};