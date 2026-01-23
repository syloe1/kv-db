#include "recovery_manager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>
#include <random>

RecoveryManager::RecoveryManager(const std::string& wal_directory)
    : wal_directory_(wal_directory), recovery_in_progress_(false),
      parallel_recovery_enabled_(true), max_parallel_segments_(4) {
    
    wal_ = std::make_unique<SegmentedWAL>(wal_directory_);
    integrity_checker_ = std::make_unique<IntegrityChecker>();
}

RecoveryManager::~RecoveryManager() {
    if (recovery_in_progress_) {
        cleanup_recovery_resources();
    }
}

RecoveryResult RecoveryManager::recover_from_crash(uint64_t checkpoint_lsn) {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    
    if (recovery_in_progress_) {
        RecoveryResult result;
        result.status = RecoveryStatus::FAILED;
        result.error_messages.push_back("Recovery already in progress");
        return result;
    }
    
    recovery_in_progress_ = true;
    current_recovery_id_ = generate_recovery_id();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // 创建恢复上下文
        RecoveryContext context;
        context.recovery_id = current_recovery_id_;
        context.target_lsn = UINT64_MAX; // 恢复到最新
        context.parallel_enabled = parallel_recovery_enabled_;
        context.max_parallel_segments = max_parallel_segments_;
        context.on_put = on_put_callback_;
        context.on_delete = on_delete_callback_;
        
        // 获取需要恢复的段
        std::vector<WALSegment*> segments = wal_->get_segments_for_recovery(checkpoint_lsn);
        
        if (segments.empty()) {
            RecoveryResult result;
            result.status = RecoveryStatus::SUCCESS;
            result.start_lsn = checkpoint_lsn;
            result.end_lsn = checkpoint_lsn;
            result.recovery_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start_time);
            
            last_recovery_result_ = result;
            recovery_in_progress_ = false;
            return result;
        }
        
        // 执行恢复
        RecoveryResult result = recover_from_segments(segments, context);
        result.recovery_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time);
        
        last_recovery_result_ = result;
        recovery_in_progress_ = false;
        
        return result;
        
    } catch (const std::exception& e) {
        RecoveryResult result;
        result.status = RecoveryStatus::FAILED;
        result.error_messages.push_back("Exception during recovery: " + std::string(e.what()));
        result.recovery_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time);
        
        last_recovery_result_ = result;
        recovery_in_progress_ = false;
        
        return result;
    }
}

RecoveryResult RecoveryManager::recover_from_backup(const std::string& backup_path) {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    
    RecoveryResult result;
    result.status = RecoveryStatus::FAILED;
    result.error_messages.push_back("Backup recovery not yet implemented");
    
    // TODO: 实现备份恢复逻辑
    // 这将在BackupManager实现后完成
    
    return result;
}

RecoveryResult RecoveryManager::recover_to_lsn(uint64_t target_lsn) {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    
    if (recovery_in_progress_) {
        RecoveryResult result;
        result.status = RecoveryStatus::FAILED;
        result.error_messages.push_back("Recovery already in progress");
        return result;
    }
    
    recovery_in_progress_ = true;
    current_recovery_id_ = generate_recovery_id();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        RecoveryContext context;
        context.recovery_id = current_recovery_id_;
        context.target_lsn = target_lsn;
        context.parallel_enabled = parallel_recovery_enabled_;
        context.max_parallel_segments = max_parallel_segments_;
        context.on_put = on_put_callback_;
        context.on_delete = on_delete_callback_;
        
        std::vector<WALSegment*> segments = wal_->get_segments_for_recovery(0);
        
        RecoveryResult result = recover_from_segments(segments, context);
        result.recovery_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time);
        
        last_recovery_result_ = result;
        recovery_in_progress_ = false;
        
        return result;
        
    } catch (const std::exception& e) {
        RecoveryResult result;
        result.status = RecoveryStatus::FAILED;
        result.error_messages.push_back("Exception during recovery: " + std::string(e.what()));
        result.recovery_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time);
        
        last_recovery_result_ = result;
        recovery_in_progress_ = false;
        
        return result;
    }
}

RecoveryResult RecoveryManager::parallel_wal_recovery(const std::vector<WALSegment*>& segments) {
    RecoveryContext context;
    context.recovery_id = generate_recovery_id();
    context.parallel_enabled = true;
    context.max_parallel_segments = max_parallel_segments_;
    context.on_put = on_put_callback_;
    context.on_delete = on_delete_callback_;
    
    return parallel_wal_recovery(segments, context);
}

RecoveryResult RecoveryManager::parallel_wal_recovery(const std::vector<WALSegment*>& segments,
                                                    const RecoveryContext& context) {
    if (!context.parallel_enabled || segments.size() <= 1) {
        // 回退到顺序恢复
        return recover_from_segments(segments, context);
    }
    
    RecoveryResult combined_result;
    combined_result.status = RecoveryStatus::SUCCESS;
    combined_result.start_lsn = UINT64_MAX;
    combined_result.end_lsn = 0;
    
    // 创建异步任务
    std::vector<std::future<RecoveryResult>> futures;
    size_t batch_size = std::min(segments.size(), context.max_parallel_segments);
    
    for (size_t i = 0; i < segments.size(); i += batch_size) {
        size_t end_idx = std::min(i + batch_size, segments.size());
        
        for (size_t j = i; j < end_idx; j++) {
            futures.push_back(recover_segment_async(segments[j], context));
        }
        
        // 等待当前批次完成
        for (auto& future : futures) {
            RecoveryResult segment_result = future.get();
            
            // 合并结果
            combined_result.recovered_entries += segment_result.recovered_entries;
            combined_result.corrupted_entries += segment_result.corrupted_entries;
            combined_result.start_lsn = std::min(combined_result.start_lsn, segment_result.start_lsn);
            combined_result.end_lsn = std::max(combined_result.end_lsn, segment_result.end_lsn);
            
            if (segment_result.status != RecoveryStatus::SUCCESS) {
                combined_result.status = RecoveryStatus::PARTIAL_SUCCESS;
                combined_result.error_messages.insert(
                    combined_result.error_messages.end(),
                    segment_result.error_messages.begin(),
                    segment_result.error_messages.end()
                );
            }
        }
        
        futures.clear();
    }
    
    return combined_result;
}

bool RecoveryManager::verify_recovery_integrity() {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    
    // 验证所有WAL段的完整性
    IntegrityStatus status = wal_->validate_all_segments();
    return status == IntegrityStatus::OK;
}

RecoveryReport RecoveryManager::generate_recovery_report() const {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    
    RecoveryReport report;
    report.recovery_id = current_recovery_id_;
    report.result = last_recovery_result_;
    
    // 获取段验证报告
    auto validation_report = wal_->validate_segments();
    report.integrity_rate = validation_report.integrity_rate;
    
    for (uint64_t segment_id : validation_report.corrupted_segment_ids) {
        report.corrupted_segments.push_back("segment_" + std::to_string(segment_id));
    }
    
    return report;
}

void RecoveryManager::print_recovery_report(const RecoveryReport& report) const {
    std::cout << "=== Recovery Report ===" << std::endl;
    std::cout << "Recovery ID: " << report.recovery_id << std::endl;
    std::cout << "Status: ";
    
    switch (report.result.status) {
        case RecoveryStatus::SUCCESS:
            std::cout << "SUCCESS" << std::endl;
            break;
        case RecoveryStatus::PARTIAL_SUCCESS:
            std::cout << "PARTIAL_SUCCESS" << std::endl;
            break;
        case RecoveryStatus::FAILED:
            std::cout << "FAILED" << std::endl;
            break;
        case RecoveryStatus::CORRUPTED_DATA:
            std::cout << "CORRUPTED_DATA" << std::endl;
            break;
        default:
            std::cout << "UNKNOWN" << std::endl;
            break;
    }
    
    std::cout << "Recovered entries: " << report.result.recovered_entries << std::endl;
    std::cout << "Corrupted entries: " << report.result.corrupted_entries << std::endl;
    std::cout << "LSN range: " << report.result.start_lsn << " - " << report.result.end_lsn << std::endl;
    std::cout << "Recovery time: " << report.result.recovery_time.count() << "ms" << std::endl;
    std::cout << "Integrity rate: " << std::fixed << std::setprecision(2) 
              << (report.integrity_rate * 100) << "%" << std::endl;
    
    if (!report.result.error_messages.empty()) {
        std::cout << "Errors:" << std::endl;
        for (const auto& error : report.result.error_messages) {
            std::cout << "  - " << error << std::endl;
        }
    }
    
    if (!report.corrupted_segments.empty()) {
        std::cout << "Corrupted segments:" << std::endl;
        for (const auto& segment : report.corrupted_segments) {
            std::cout << "  - " << segment << std::endl;
        }
    }
    
    std::cout << "======================" << std::endl;
}

void RecoveryManager::handle_recovery_failure(const RecoveryError& error) {
    std::string error_desc = get_error_description(error);
    std::cout << "Recovery failure: " << error_desc << std::endl;
    
    // 根据错误类型采取相应措施
    switch (error) {
        case RecoveryError::CHECKPOINT_CORRUPTED:
            std::cout << "Attempting recovery from earlier checkpoint or full WAL..." << std::endl;
            break;
        case RecoveryError::WAL_SEGMENT_MISSING:
            std::cout << "Skipping missing segment, data loss may occur..." << std::endl;
            break;
        case RecoveryError::BACKUP_CHAIN_BROKEN:
            std::cout << "Attempting recovery from most recent complete backup..." << std::endl;
            break;
        case RecoveryError::INSUFFICIENT_SPACE:
            std::cout << "Please free up disk space and retry recovery..." << std::endl;
            break;
        case RecoveryError::PERMISSION_DENIED:
            std::cout << "Entering read-only mode due to permission issues..." << std::endl;
            break;
        default:
            std::cout << "Unknown error, please check system logs..." << std::endl;
            break;
    }
}

std::string RecoveryManager::get_error_description(RecoveryError error) const {
    switch (error) {
        case RecoveryError::CHECKPOINT_CORRUPTED:
            return "Checkpoint file is corrupted";
        case RecoveryError::WAL_SEGMENT_MISSING:
            return "One or more WAL segments are missing";
        case RecoveryError::BACKUP_CHAIN_BROKEN:
            return "Backup chain is broken or incomplete";
        case RecoveryError::INSUFFICIENT_SPACE:
            return "Insufficient disk space for recovery";
        case RecoveryError::PERMISSION_DENIED:
            return "Permission denied accessing recovery files";
        default:
            return "Unknown recovery error";
    }
}

// 配置方法
void RecoveryManager::set_parallel_recovery_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    parallel_recovery_enabled_ = enabled;
}

void RecoveryManager::set_max_parallel_segments(size_t max_segments) {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    max_parallel_segments_ = max_segments;
}

void RecoveryManager::set_recovery_callbacks(
    const std::function<void(const std::string&, const std::string&)>& on_put,
    const std::function<void(const std::string&)>& on_delete) {
    
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    on_put_callback_ = on_put;
    on_delete_callback_ = on_delete;
}

// 状态查询方法
bool RecoveryManager::is_recovery_in_progress() const {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    return recovery_in_progress_;
}

RecoveryStatus RecoveryManager::get_last_recovery_status() const {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    return last_recovery_result_.status;
}

std::string RecoveryManager::get_current_recovery_id() const {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    return current_recovery_id_;
}

// 私有方法实现
std::string RecoveryManager::generate_recovery_id() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << "recovery_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    
    // 添加随机后缀
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    ss << "_" << dis(gen);
    
    return ss.str();
}

RecoveryResult RecoveryManager::recover_from_segments(const std::vector<WALSegment*>& segments,
                                                    const RecoveryContext& context) {
    if (context.parallel_enabled && segments.size() > 1) {
        return parallel_wal_recovery(segments, context);
    }
    
    // 顺序恢复
    RecoveryResult combined_result;
    combined_result.status = RecoveryStatus::SUCCESS;
    combined_result.start_lsn = UINT64_MAX;
    combined_result.end_lsn = 0;
    
    for (WALSegment* segment : segments) {
        RecoveryResult segment_result = recover_segment_sequential(segment, context);
        
        // 合并结果
        combined_result.recovered_entries += segment_result.recovered_entries;
        combined_result.corrupted_entries += segment_result.corrupted_entries;
        combined_result.start_lsn = std::min(combined_result.start_lsn, segment_result.start_lsn);
        combined_result.end_lsn = std::max(combined_result.end_lsn, segment_result.end_lsn);
        
        if (segment_result.status != RecoveryStatus::SUCCESS) {
            combined_result.status = RecoveryStatus::PARTIAL_SUCCESS;
            combined_result.error_messages.insert(
                combined_result.error_messages.end(),
                segment_result.error_messages.begin(),
                segment_result.error_messages.end()
            );
        }
    }
    
    return combined_result;
}

RecoveryResult RecoveryManager::recover_segment_sequential(WALSegment* segment,
                                                         const RecoveryContext& context) {
    RecoveryResult result;
    result.status = RecoveryStatus::SUCCESS;
    
    try {
        // 验证段完整性
        IntegrityStatus integrity_status = segment->validate_segment();
        if (integrity_status != IntegrityStatus::OK) {
            result.status = RecoveryStatus::CORRUPTED_DATA;
            result.error_messages.push_back("Segment integrity validation failed");
            return result;
        }
        
        // 读取段中的所有条目
        std::vector<WALEntry> entries = segment->read_entries_for_recovery();
        
        result.start_lsn = segment->get_start_lsn();
        result.end_lsn = segment->get_end_lsn();
        
        for (const WALEntry& entry : entries) {
            if (entry.lsn > context.target_lsn) {
                break; // 超过目标LSN，停止恢复
            }
            
            if (entry.verify_crc32()) {
                apply_wal_entry(entry, context);
                result.recovered_entries++;
            } else {
                result.corrupted_entries++;
                result.status = RecoveryStatus::PARTIAL_SUCCESS;
                result.error_messages.push_back("Corrupted entry at LSN " + std::to_string(entry.lsn));
            }
        }
        
    } catch (const std::exception& e) {
        result.status = RecoveryStatus::FAILED;
        result.error_messages.push_back("Exception in segment recovery: " + std::string(e.what()));
    }
    
    return result;
}

std::future<RecoveryResult> RecoveryManager::recover_segment_async(WALSegment* segment,
                                                                 const RecoveryContext& context) {
    return std::async(std::launch::async, [this, segment, context]() {
        return recover_segment_sequential(segment, context);
    });
}

void RecoveryManager::apply_wal_entry(const WALEntry& entry, const RecoveryContext& context) {
    if (!context.on_put && !context.on_delete) {
        return; // 没有回调函数，跳过应用
    }
    
    std::string data_str(entry.data.begin(), entry.data.end());
    
    if (entry.entry_type == WALEntry::PUT && context.on_put) {
        // 解析 "key value" 格式
        std::istringstream iss(data_str);
        std::string key, value;
        iss >> key >> value;
        context.on_put(key, value);
    } else if (entry.entry_type == WALEntry::DELETE && context.on_delete) {
        // 删除操作，data_str就是key
        context.on_delete(data_str);
    }
}

bool RecoveryManager::validate_recovery_prerequisites(const RecoveryContext& context) {
    // 验证恢复前提条件
    if (context.target_lsn == 0) {
        return false;
    }
    
    // 检查WAL目录是否存在
    if (!std::filesystem::exists(wal_directory_)) {
        return false;
    }
    
    return true;
}

void RecoveryManager::update_recovery_progress(const std::string& message) {
    std::cout << "[Recovery " << current_recovery_id_ << "] " << message << std::endl;
}

void RecoveryManager::cleanup_recovery_resources() {
    recovery_in_progress_ = false;
    current_recovery_id_.clear();
}