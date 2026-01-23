#include "backup_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <random>

namespace fs = std::filesystem;

BackupManager::BackupManager(const std::string& backup_directory, 
                           const std::string& data_directory)
    : backup_directory_(backup_directory), data_directory_(data_directory),
      backup_in_progress_(false), current_lsn_(0),
      compression_enabled_(false), max_backup_size_mb_(1024),
      backup_retention_days_(30) {
    
    // 创建备份目录
    fs::create_directories(backup_directory_);
    
    integrity_checker_ = std::make_unique<IntegrityChecker>();
    
    // 加载现有备份
    load_all_backups();
}

BackupManager::~BackupManager() {
    // 清理资源
}

BackupResult BackupManager::create_full_backup(const std::string& backup_path) {
    return create_backup_internal(BackupType::FULL, backup_path);
}

BackupResult BackupManager::create_incremental_backup(const std::string& backup_path) {
    // 找到最新的完整备份作为基础
    std::string base_backup_id;
    for (const auto& backup : backups_) {
        if (backup.type == BackupType::FULL && backup.status == BackupStatus::COMPLETED) {
            base_backup_id = backup.backup_id;
            break;
        }
    }
    
    if (base_backup_id.empty()) {
        BackupResult result;
        result.success = false;
        result.error_messages.push_back("No full backup found for incremental backup");
        return result;
    }
    
    return create_incremental_backup(backup_path, base_backup_id);
}

BackupResult BackupManager::create_incremental_backup(const std::string& backup_path,
                                                    const std::string& base_backup_id) {
    return create_backup_internal(BackupType::INCREMENTAL, backup_path, base_backup_id);
}

RestoreResult BackupManager::restore_from_backup_chain(const std::string& backup_path) {
    std::lock_guard<std::mutex> lock(backup_mutex_);
    
    RestoreResult result;
    
    // 分析备份链
    BackupChainInfo chain_info = analyze_backup_chain(backup_path);
    if (!chain_info.is_complete) {
        result.success = false;
        result.error_messages.push_back("Backup chain is incomplete");
        return result;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // 构建备份应用顺序
        std::vector<std::string> backup_ids;
        backup_ids.push_back(chain_info.full_backup_id);
        backup_ids.insert(backup_ids.end(), 
                         chain_info.incremental_backups.begin(),
                         chain_info.incremental_backups.end());
        
        // 应用备份链
        bool success = apply_backup_chain(backup_ids, data_directory_);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.restore_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        if (success) {
            result.success = true;
            result.backup_id = chain_info.full_backup_id;
            result.restored_size = chain_info.total_size;
        } else {
            result.success = false;
            result.error_messages.push_back("Failed to apply backup chain");
        }
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_messages.push_back("Exception during restore: " + std::string(e.what()));
    }
    
    return result;
}

RestoreResult BackupManager::restore_from_backup(const std::string& backup_id) {
    std::lock_guard<std::mutex> lock(backup_mutex_);
    
    RestoreResult result;
    
    // 查找备份元数据
    auto it = std::find_if(backups_.begin(), backups_.end(),
                          [&backup_id](const BackupMetadata& metadata) {
                              return metadata.backup_id == backup_id;
                          });
    
    if (it == backups_.end()) {
        result.success = false;
        result.error_messages.push_back("Backup not found: " + backup_id);
        return result;
    }
    
    const BackupMetadata& metadata = *it;
    
    if (metadata.status != BackupStatus::COMPLETED) {
        result.success = false;
        result.error_messages.push_back("Backup is not in completed state");
        return result;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        bool success = restore_files_from_backup(metadata, data_directory_);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.restore_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        if (success) {
            result.success = true;
            result.backup_id = backup_id;
            result.restored_size = metadata.backup_size;
            result.restored_files = metadata.files.size();
        } else {
            result.success = false;
            result.error_messages.push_back("Failed to restore files from backup");
        }
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_messages.push_back("Exception during restore: " + std::string(e.what()));
    }
    
    return result;
}

RestoreResult BackupManager::restore_to_lsn(uint64_t target_lsn) {
    // 找到包含目标LSN的备份链
    std::string best_backup_id;
    uint64_t best_lsn = 0;
    
    for (const auto& backup : backups_) {
        if (backup.end_lsn <= target_lsn && backup.end_lsn > best_lsn) {
            best_backup_id = backup.backup_id;
            best_lsn = backup.end_lsn;
        }
    }
    
    if (best_backup_id.empty()) {
        RestoreResult result;
        result.success = false;
        result.error_messages.push_back("No suitable backup found for target LSN");
        return result;
    }
    
    return restore_from_backup(best_backup_id);
}