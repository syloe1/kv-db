#include "checkpoint_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <random>

namespace fs = std::filesystem;

CheckpointManager::CheckpointManager(const std::string& checkpoint_directory)
    : checkpoint_directory_(checkpoint_directory), checkpoint_in_progress_(false),
      auto_checkpoint_running_(false) {
    
    // 创建检查点目录
    fs::create_directories(checkpoint_directory_);
    
    integrity_checker_ = std::make_unique<IntegrityChecker>();
    
    // 加载现有检查点
    load_all_checkpoints();
}

CheckpointManager::~CheckpointManager() {
    stop_auto_checkpoint();
}

CheckpointResult CheckpointManager::create_checkpoint() {
    return create_checkpoint(CheckpointTrigger::MANUAL, "Manual checkpoint");
}

CheckpointResult CheckpointManager::create_checkpoint(CheckpointTrigger trigger) {
    std::string description;
    switch (trigger) {
        case CheckpointTrigger::TIME_INTERVAL:
            description = "Automatic checkpoint (time interval)";
            break;
        case CheckpointTrigger::TRANSACTION_COUNT:
            description = "Automatic checkpoint (transaction count)";
            break;
        case CheckpointTrigger::WAL_SIZE:
            description = "Automatic checkpoint (WAL size)";
            break;
        case CheckpointTrigger::SYSTEM_SHUTDOWN:
            description = "System shutdown checkpoint";
            break;
        default:
            description = "Manual checkpoint";
            break;
    }
    
    return create_checkpoint(trigger, description);
}

CheckpointResult CheckpointManager::create_checkpoint(CheckpointTrigger trigger, 
                                                    const std::string& description) {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    
    CheckpointResult result;
    
    if (checkpoint_in_progress_) {
        result.success = false;
        result.error_messages.push_back("Checkpoint creation already in progress");
        return result;
    }
    
    if (!database_snapshot_) {
        result.success = false;
        result.error_messages.push_back("Database snapshot interface not set");
        return result;
    }
    
    checkpoint_in_progress_ = true;
    current_checkpoint_id_ = generate_checkpoint_id();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        bool success = create_checkpoint_internal(trigger, description, result);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.creation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        if (success) {
            result.success = true;
            result.checkpoint_id = current_checkpoint_id_;
            
            // 清理旧检查点
            if (config_.max_checkpoints_to_keep > 0) {
                cleanup_old_checkpoints(config_.max_checkpoints_to_keep);
            }
        }
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_messages.push_back("Exception during checkpoint creation: " + 
                                       std::string(e.what()));
    }
    
    checkpoint_in_progress_ = false;
    current_checkpoint_id_.clear();
    
    return result;
}

std::vector<CheckpointInfo> CheckpointManager::list_checkpoints() const {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    return checkpoints_;
}

CheckpointInfo CheckpointManager::get_checkpoint_info(const std::string& checkpoint_id) const {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    
    auto it = std::find_if(checkpoints_.begin(), checkpoints_.end(),
                          [&checkpoint_id](const CheckpointInfo& info) {
                              return info.checkpoint_id == checkpoint_id;
                          });
    
    if (it != checkpoints_.end()) {
        return *it;
    }
    
    return CheckpointInfo(); // 返回空的检查点信息
}

bool CheckpointManager::restore_from_checkpoint(const std::string& checkpoint_id) {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    
    if (!database_snapshot_) {
        std::cout << "Database snapshot interface not set" << std::endl;
        return false;
    }
    
    CheckpointInfo info = get_checkpoint_info(checkpoint_id);
    if (info.checkpoint_id.empty()) {
        std::cout << "Checkpoint not found: " << checkpoint_id << std::endl;
        return false;
    }
    
    if (info.status != CheckpointStatus::COMPLETED) {
        std::cout << "Checkpoint is not in completed state: " << checkpoint_id << std::endl;
        return false;
    }
    
    // 验证检查点完整性
    if (!verify_checkpoint_integrity(info)) {
        std::cout << "Checkpoint integrity verification failed: " << checkpoint_id << std::endl;
        return false;
    }
    
    // 执行恢复
    try {
        bool success = database_snapshot_->restore_state(info.file_path);
        if (success) {
            std::cout << "Successfully restored from checkpoint: " << checkpoint_id << std::endl;
        } else {
            std::cout << "Failed to restore from checkpoint: " << checkpoint_id << std::endl;
        }
        return success;
        
    } catch (const std::exception& e) {
        std::cout << "Exception during checkpoint restore: " << e.what() << std::endl;
        return false;
    }
}

bool CheckpointManager::delete_checkpoint(const std::string& checkpoint_id) {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    
    auto it = std::find_if(checkpoints_.begin(), checkpoints_.end(),
                          [&checkpoint_id](const CheckpointInfo& info) {
                              return info.checkpoint_id == checkpoint_id;
                          });
    
    if (it == checkpoints_.end()) {
        return false;
    }
    
    // 删除文件
    cleanup_checkpoint_files(checkpoint_id);
    
    // 从列表中移除
    checkpoints_.erase(it);
    
    return true;
}

void CheckpointManager::cleanup_old_checkpoints(size_t max_checkpoints) {
    if (checkpoints_.size() <= max_checkpoints) {
        return;
    }
    
    // 按创建时间排序，保留最新的
    std::sort(checkpoints_.begin(), checkpoints_.end(),
              [](const CheckpointInfo& a, const CheckpointInfo& b) {
                  return a.creation_time > b.creation_time;
              });
    
    // 删除多余的检查点
    while (checkpoints_.size() > max_checkpoints) {
        CheckpointInfo& oldest = checkpoints_.back();
        cleanup_checkpoint_files(oldest.checkpoint_id);
        checkpoints_.pop_back();
    }
}

void CheckpointManager::cleanup_expired_checkpoints() {
    // 这里可以实现基于时间的过期清理逻辑
    // 目前暂时不实现
}

void CheckpointManager::configure_auto_checkpoint(const CheckpointConfig& config) {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    
    bool was_running = auto_checkpoint_running_;
    if (was_running) {
        stop_auto_checkpoint();
    }
    
    config_ = config;
    
    if (was_running && config_.auto_checkpoint_enabled) {
        start_auto_checkpoint();
    }
}

void CheckpointManager::start_auto_checkpoint() {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    
    if (auto_checkpoint_running_ || !config_.auto_checkpoint_enabled) {
        return;
    }
    
    auto_checkpoint_running_ = true;
    auto_checkpoint_thread_ = std::thread(&CheckpointManager::auto_checkpoint_worker, this);
}

void CheckpointManager::stop_auto_checkpoint() {
    {
        std::lock_guard<std::mutex> lock(checkpoint_mutex_);
        auto_checkpoint_running_ = false;
    }
    
    if (auto_checkpoint_thread_.joinable()) {
        auto_checkpoint_thread_.join();
    }
}

bool CheckpointManager::is_auto_checkpoint_running() const {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    return auto_checkpoint_running_;
}

bool CheckpointManager::validate_checkpoint(const std::string& checkpoint_id) {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    
    CheckpointInfo info = get_checkpoint_info(checkpoint_id);
    if (info.checkpoint_id.empty()) {
        return false;
    }
    
    return verify_checkpoint_integrity(info);
}

std::vector<std::string> CheckpointManager::find_corrupted_checkpoints() {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    
    std::vector<std::string> corrupted;
    
    for (auto& info : checkpoints_) {
        if (!verify_checkpoint_integrity(info)) {
            corrupted.push_back(info.checkpoint_id);
            info.status = CheckpointStatus::CORRUPTED;
        }
    }
    
    return corrupted;
}

bool CheckpointManager::repair_checkpoint(const std::string& checkpoint_id) {
    // 检查点修复功能暂时不实现
    // 通常需要从备份或其他检查点重建
    return false;
}

void CheckpointManager::set_database_snapshot(std::shared_ptr<DatabaseSnapshot> snapshot) {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    database_snapshot_ = snapshot;
}

bool CheckpointManager::is_checkpoint_in_progress() const {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    return checkpoint_in_progress_;
}

std::string CheckpointManager::get_current_checkpoint_id() const {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    return current_checkpoint_id_;
}

CheckpointConfig CheckpointManager::get_current_config() const {
    std::lock_guard<std::mutex> lock(checkpoint_mutex_);
    return config_;
}

// 私有方法实现
std::string CheckpointManager::generate_checkpoint_id() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << "checkpoint_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    
    // 添加随机后缀
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    ss << "_" << dis(gen);
    
    return ss.str();
}

std::string CheckpointManager::get_checkpoint_file_path(const std::string& checkpoint_id) const {
    return checkpoint_directory_ + "/" + checkpoint_id + ".checkpoint";
}

std::string CheckpointManager::get_checkpoint_meta_path(const std::string& checkpoint_id) const {
    return checkpoint_directory_ + "/" + checkpoint_id + ".meta";
}

bool CheckpointManager::create_checkpoint_internal(CheckpointTrigger trigger,
                                                  const std::string& description,
                                                  CheckpointResult& result) {
    // 创建检查点信息
    CheckpointInfo info;
    info.checkpoint_id = current_checkpoint_id_;
    info.creation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    info.trigger = trigger;
    info.description = description;
    info.status = CheckpointStatus::CREATING;
    info.file_path = get_checkpoint_file_path(current_checkpoint_id_);
    
    // 获取当前LSN
    info.lsn = database_snapshot_->get_current_lsn();
    
    // 创建数据库状态快照
    if (!database_snapshot_->capture_state(info.file_path)) {
        result.error_messages.push_back("Failed to capture database state");
        return false;
    }
    
    // 计算文件大小和校验和
    if (fs::exists(info.file_path)) {
        info.file_size = fs::file_size(info.file_path);
        
        if (!calculate_checkpoint_checksum(info.file_path, info.file_crc32)) {
            result.error_messages.push_back("Failed to calculate checkpoint checksum");
            return false;
        }
    } else {
        result.error_messages.push_back("Checkpoint file was not created");
        return false;
    }
    
    // 更新状态
    info.status = CheckpointStatus::COMPLETED;
    
    // 保存元数据
    if (!save_checkpoint_metadata(info)) {
        result.error_messages.push_back("Failed to save checkpoint metadata");
        return false;
    }
    
    // 添加到检查点列表
    checkpoints_.push_back(info);
    
    // 设置结果
    result.lsn = info.lsn;
    result.file_path = info.file_path;
    result.file_size = info.file_size;
    
    return true;
}

bool CheckpointManager::save_checkpoint_metadata(const CheckpointInfo& info) {
    std::string meta_path = get_checkpoint_meta_path(info.checkpoint_id);
    
    std::ofstream file(meta_path);
    if (!file.is_open()) {
        return false;
    }
    
    file << "checkpoint_id=" << info.checkpoint_id << std::endl;
    file << "lsn=" << info.lsn << std::endl;
    file << "creation_time=" << info.creation_time << std::endl;
    file << "file_size=" << info.file_size << std::endl;
    file << "file_crc32=" << info.file_crc32 << std::endl;
    file << "file_path=" << info.file_path << std::endl;
    file << "status=" << static_cast<int>(info.status) << std::endl;
    file << "trigger=" << static_cast<int>(info.trigger) << std::endl;
    file << "description=" << info.description << std::endl;
    
    return file.good();
}

bool CheckpointManager::load_checkpoint_metadata(const std::string& checkpoint_id, 
                                                CheckpointInfo& info) {
    std::string meta_path = get_checkpoint_meta_path(checkpoint_id);
    
    if (!fs::exists(meta_path)) {
        return false;
    }
    
    std::ifstream file(meta_path);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        if (key == "checkpoint_id") {
            info.checkpoint_id = value;
        } else if (key == "lsn") {
            info.lsn = std::stoull(value);
        } else if (key == "creation_time") {
            info.creation_time = std::stoull(value);
        } else if (key == "file_size") {
            info.file_size = std::stoull(value);
        } else if (key == "file_crc32") {
            info.file_crc32 = std::stoul(value);
        } else if (key == "file_path") {
            info.file_path = value;
        } else if (key == "status") {
            info.status = static_cast<CheckpointStatus>(std::stoi(value));
        } else if (key == "trigger") {
            info.trigger = static_cast<CheckpointTrigger>(std::stoi(value));
        } else if (key == "description") {
            info.description = value;
        }
    }
    
    return true;
}

bool CheckpointManager::load_all_checkpoints() {
    checkpoints_.clear();
    
    if (!fs::exists(checkpoint_directory_)) {
        return true;
    }
    
    for (const auto& entry : fs::directory_iterator(checkpoint_directory_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".meta") {
            std::string checkpoint_id = entry.path().stem().string();
            
            CheckpointInfo info;
            if (load_checkpoint_metadata(checkpoint_id, info)) {
                checkpoints_.push_back(info);
            }
        }
    }
    
    // 按创建时间排序
    std::sort(checkpoints_.begin(), checkpoints_.end(),
              [](const CheckpointInfo& a, const CheckpointInfo& b) {
                  return a.creation_time > b.creation_time;
              });
    
    return true;
}

void CheckpointManager::auto_checkpoint_worker() {
    while (auto_checkpoint_running_) {
        std::this_thread::sleep_for(std::chrono::minutes(1)); // 每分钟检查一次
        
        if (!auto_checkpoint_running_) break;
        
        if (should_create_checkpoint()) {
            CheckpointTrigger trigger = determine_checkpoint_trigger();
            create_checkpoint(trigger);
        }
    }
}

bool CheckpointManager::should_create_checkpoint() const {
    if (!config_.auto_checkpoint_enabled || !database_snapshot_) {
        return false;
    }
    
    // 检查时间间隔
    if (!checkpoints_.empty()) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto last_checkpoint_time = checkpoints_[0].creation_time;
        auto elapsed_minutes = (now - last_checkpoint_time) / (1000 * 60);
        
        if (elapsed_minutes >= config_.time_interval.count()) {
            return true;
        }
    } else {
        return true; // 没有检查点，创建第一个
    }
    
    // 这里可以添加其他触发条件的检查
    // 比如事务数量、WAL大小等
    
    return false;
}

CheckpointTrigger CheckpointManager::determine_checkpoint_trigger() const {
    // 简化实现，主要基于时间间隔
    return CheckpointTrigger::TIME_INTERVAL;
}

bool CheckpointManager::copy_database_files(const std::string& checkpoint_path) {
    // 这个方法在实际实现中会复制数据库文件
    // 目前简化实现
    return true;
}

bool CheckpointManager::calculate_checkpoint_checksum(const std::string& checkpoint_path, 
                                                    uint32_t& checksum) {
    if (!fs::exists(checkpoint_path)) {
        return false;
    }
    
    std::ifstream file(checkpoint_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // 读取文件内容并计算CRC32
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    
    checksum = IntegrityChecker::CalculateCRC32(buffer.data(), buffer.size());
    return true;
}

bool CheckpointManager::verify_checkpoint_integrity(const CheckpointInfo& info) {
    if (!fs::exists(info.file_path)) {
        return false;
    }
    
    // 验证文件大小
    uint64_t actual_size = fs::file_size(info.file_path);
    if (actual_size != info.file_size) {
        return false;
    }
    
    // 验证CRC32
    uint32_t actual_crc32;
    if (!calculate_checkpoint_checksum(info.file_path, actual_crc32)) {
        return false;
    }
    
    return actual_crc32 == info.file_crc32;
}

void CheckpointManager::cleanup_checkpoint_files(const std::string& checkpoint_id) {
    std::string checkpoint_file = get_checkpoint_file_path(checkpoint_id);
    std::string meta_file = get_checkpoint_meta_path(checkpoint_id);
    
    if (fs::exists(checkpoint_file)) {
        fs::remove(checkpoint_file);
    }
    
    if (fs::exists(meta_file)) {
        fs::remove(meta_file);
    }
}

uint64_t CheckpointManager::get_directory_size(const std::string& directory) const {
    uint64_t size = 0;
    
    if (fs::exists(directory)) {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                size += fs::file_size(entry);
            }
        }
    }
    
    return size;
}