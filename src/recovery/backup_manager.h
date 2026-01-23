#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include "integrity_checker.h"

// 备份类型
enum class BackupType {
    FULL,        // 完整备份
    INCREMENTAL, // 增量备份
    DIFFERENTIAL // 差异备份
};

// 备份状态
enum class BackupStatus {
    CREATING,
    COMPLETED,
    FAILED,
    CORRUPTED,
    EXPIRED
};

// 备份元数据
struct BackupMetadata {
    std::string backup_id;
    BackupType type;
    uint64_t base_lsn;
    uint64_t end_lsn;
    uint64_t creation_time;
    std::vector<std::string> files;
    std::string parent_backup_id;
    uint32_t metadata_crc32;
    BackupStatus status;
    uint64_t backup_size;
    std::string backup_path;
    std::string description;
    
    BackupMetadata() : type(BackupType::FULL), base_lsn(0), end_lsn(0),
                      creation_time(0), metadata_crc32(0), 
                      status(BackupStatus::CREATING), backup_size(0) {}
};

// 备份结果
struct BackupResult {
    bool success;
    std::string backup_id;
    BackupType type;
    std::string backup_path;
    uint64_t backup_size;
    uint64_t files_count;
    std::chrono::milliseconds creation_time;
    std::vector<std::string> error_messages;
    
    BackupResult() : success(false), type(BackupType::FULL), 
                    backup_size(0), files_count(0), creation_time(0) {}
};

// 恢复结果
struct RestoreResult {
    bool success;
    std::string backup_id;
    uint64_t restored_files;
    uint64_t restored_size;
    std::chrono::milliseconds restore_time;
    std::vector<std::string> error_messages;
    
    RestoreResult() : success(false), restored_files(0), 
                     restored_size(0), restore_time(0) {}
};

// 备份信息
struct BackupInfo {
    BackupMetadata metadata;
    bool is_valid;
    std::vector<std::string> dependencies; // 依赖的备份ID
    
    BackupInfo() : is_valid(false) {}
};

// 备份链信息
struct BackupChainInfo {
    std::string full_backup_id;
    std::vector<std::string> incremental_backups;
    bool is_complete;
    uint64_t total_size;
    uint64_t chain_length;
    
    BackupChainInfo() : is_complete(false), total_size(0), chain_length(0) {}
};

// 文件变更跟踪
struct FileChangeInfo {
    std::string file_path;
    uint64_t last_modified_lsn;
    uint64_t file_size;
    uint32_t file_crc32;
    std::chrono::system_clock::time_point last_modified_time;
};

class BackupManager {
public:
    BackupManager(const std::string& backup_directory, const std::string& data_directory);
    ~BackupManager();
    
    // 备份操作
    BackupResult create_full_backup(const std::string& backup_path);
    BackupResult create_incremental_backup(const std::string& backup_path);
    BackupResult create_incremental_backup(const std::string& backup_path, 
                                         const std::string& base_backup_id);
    
    // 恢复操作
    RestoreResult restore_from_backup_chain(const std::string& backup_path);
    RestoreResult restore_from_backup(const std::string& backup_id);
    RestoreResult restore_to_lsn(uint64_t target_lsn);
    
    // 备份管理
    std::vector<BackupInfo> list_backups(const std::string& backup_directory) const;
    BackupInfo get_backup_info(const std::string& backup_id) const;
    BackupChainInfo analyze_backup_chain(const std::string& backup_path) const;
    bool delete_backup(const std::string& backup_id);
    void cleanup_old_backups(size_t max_backups_to_keep);
    
    // LSN跟踪
    void track_file_changes(const std::string& file_path, uint64_t lsn);
    std::vector<std::string> get_changed_files_since_lsn(uint64_t lsn) const;
    std::vector<FileChangeInfo> get_file_changes_since_lsn(uint64_t lsn) const;
    void update_file_tracking(const std::string& file_path);
    
    // 验证和修复
    bool validate_backup(const std::string& backup_id);
    std::vector<std::string> find_corrupted_backups();
    bool repair_backup_chain(const std::string& backup_id);
    
    // 配置
    void set_compression_enabled(bool enabled);
    void set_max_backup_size(uint64_t max_size_mb);
    void set_backup_retention_days(int days);
    
    // 状态查询
    bool is_backup_in_progress() const;
    std::string get_current_backup_id() const;
    uint64_t get_total_backup_size() const;

private:
    std::string backup_directory_;
    std::string data_directory_;
    std::unique_ptr<IntegrityChecker> integrity_checker_;
    
    // 状态管理
    mutable std::mutex backup_mutex_;
    bool backup_in_progress_;
    std::string current_backup_id_;
    
    // 文件跟踪
    std::unordered_map<std::string, FileChangeInfo> file_changes_;
    uint64_t current_lsn_;
    
    // 备份存储
    std::vector<BackupMetadata> backups_;
    
    // 配置
    bool compression_enabled_;
    uint64_t max_backup_size_mb_;
    int backup_retention_days_;
    
    // 私有方法
    std::string generate_backup_id() const;
    std::string get_backup_metadata_path(const std::string& backup_id) const;
    std::string get_backup_data_path(const std::string& backup_id) const;
    
    BackupResult create_backup_internal(BackupType type, const std::string& backup_path,
                                       const std::string& base_backup_id = "");
    
    bool save_backup_metadata(const BackupMetadata& metadata);
    bool load_backup_metadata(const std::string& backup_id, BackupMetadata& metadata);
    bool load_all_backups();
    
    std::vector<std::string> get_files_to_backup(BackupType type, uint64_t base_lsn);
    bool copy_files_to_backup(const std::vector<std::string>& files, 
                             const std::string& backup_path);
    bool compress_backup(const std::string& backup_path);
    
    bool restore_files_from_backup(const BackupMetadata& metadata, 
                                  const std::string& target_directory);
    bool apply_backup_chain(const std::vector<std::string>& backup_ids,
                           const std::string& target_directory);
    
    uint32_t calculate_metadata_checksum(const BackupMetadata& metadata);
    bool verify_backup_integrity(const BackupMetadata& metadata);
    
    void cleanup_backup_files(const std::string& backup_id);
    bool is_backup_expired(const BackupMetadata& metadata) const;
    
    std::vector<std::string> find_backup_dependencies(const std::string& backup_id) const;
    bool validate_backup_chain(const std::vector<std::string>& chain) const;
};