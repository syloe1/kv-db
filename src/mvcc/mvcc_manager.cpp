#include "mvcc_manager.h"
#include <iostream>
#include <algorithm>
#include <shared_mutex>

// VersionChain 实现
VersionChain::VersionChain(const std::string& key) : key_(key) {}

VersionChain::~VersionChain() {}

bool VersionChain::add_version(const VersionedValue& version) {
    std::unique_lock<std::shared_mutex> lock(versions_mutex_);
    
    auto new_version = std::make_unique<VersionedValue>(version);
    versions_.push_back(std::move(new_version));
    
    // 保持版本按时间戳排序
    sort_versions_by_timestamp();
    
    return true;
}

VersionedValue* VersionChain::get_version(uint64_t read_timestamp) {
    std::shared_lock<std::shared_mutex> lock(versions_mutex_);
    return find_version_for_timestamp(read_timestamp);
}

VersionedValue* VersionChain::get_latest_version() {
    std::shared_lock<std::shared_mutex> lock(versions_mutex_);
    
    if (versions_.empty()) {
        return nullptr;
    }
    
    // 返回最新的已提交版本
    for (auto it = versions_.rbegin(); it != versions_.rend(); ++it) {
        if ((*it)->is_committed) {
            return it->get();
        }
    }
    
    return nullptr;
}

std::vector<VersionedValue*> VersionChain::get_all_versions() {
    std::shared_lock<std::shared_mutex> lock(versions_mutex_);
    
    std::vector<VersionedValue*> result;
    for (const auto& version : versions_) {
        result.push_back(version.get());
    }
    
    return result;
}

bool VersionChain::mark_deleted(uint64_t version, uint64_t delete_timestamp) {
    std::unique_lock<std::shared_mutex> lock(versions_mutex_);
    
    for (auto& ver : versions_) {
        if (ver->version == version) {
            ver->delete_timestamp = delete_timestamp;
            return true;
        }
    }
    
    return false;
}

size_t VersionChain::cleanup_old_versions(uint64_t min_active_timestamp) {
    std::unique_lock<std::shared_mutex> lock(versions_mutex_);
    
    size_t cleaned = 0;
    auto it = versions_.begin();
    
    while (it != versions_.end()) {
        // 保留至少一个版本，且不能删除仍可能被读取的版本
        if (versions_.size() > 1 && 
            (*it)->create_timestamp < min_active_timestamp &&
            ((*it)->delete_timestamp == 0 || (*it)->delete_timestamp < min_active_timestamp)) {
            
            it = versions_.erase(it);
            cleaned++;
        } else {
            ++it;
        }
    }
    
    return cleaned;
}

size_t VersionChain::get_version_count() const {
    std::shared_lock<std::shared_mutex> lock(versions_mutex_);
    return versions_.size();
}

uint64_t VersionChain::get_oldest_version() const {
    std::shared_lock<std::shared_mutex> lock(versions_mutex_);
    
    if (versions_.empty()) {
        return 0;
    }
    
    return versions_.front()->version;
}

uint64_t VersionChain::get_latest_version_number() const {
    std::shared_lock<std::shared_mutex> lock(versions_mutex_);
    
    if (versions_.empty()) {
        return 0;
    }
    
    return versions_.back()->version;
}

VersionedValue* VersionChain::find_version_for_timestamp(uint64_t timestamp) {
    // 找到对指定时间戳可见的最新版本
    VersionedValue* result = nullptr;
    
    for (auto it = versions_.rbegin(); it != versions_.rend(); ++it) {
        if ((*it)->is_visible_to(timestamp)) {
            result = it->get();
            break;
        }
    }
    
    return result;
}

void VersionChain::sort_versions_by_timestamp() {
    std::sort(versions_.begin(), versions_.end(),
              [](const std::unique_ptr<VersionedValue>& a, 
                 const std::unique_ptr<VersionedValue>& b) {
                  return a->create_timestamp < b->create_timestamp;
              });
}

// MVCCManager 实现
MVCCManager::MVCCManager() 
    : global_timestamp_(1), gc_running_(false), 
      gc_interval_(std::chrono::minutes(5)),
      max_versions_per_key_(10),
      version_retention_time_(std::chrono::hours(1)),
      gc_runs_(0), versions_cleaned_(0) {
}

MVCCManager::~MVCCManager() {
    stop_garbage_collection();
}

std::string MVCCManager::read(const std::string& key, uint64_t read_timestamp) {
    std::string value;
    if (read(key, read_timestamp, value)) {
        return value;
    }
    return "";
}

bool MVCCManager::read(const std::string& key, uint64_t read_timestamp, std::string& value) {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    
    auto it = version_chains_.find(key);
    if (it == version_chains_.end()) {
        return false; // 键不存在
    }
    
    VersionedValue* version = it->second->get_version(read_timestamp);
    if (version && !version->is_deleted_at(read_timestamp)) {
        value = version->value;
        return true;
    }
    
    return false;
}

bool MVCCManager::write(const std::string& key, const std::string& value,
                       uint64_t transaction_id, uint64_t write_timestamp) {
    VersionChain* chain = get_or_create_version_chain(key);
    
    VersionedValue new_version(value, generate_timestamp(), write_timestamp, transaction_id);
    
    // 注册活跃事务
    register_active_transaction(transaction_id, write_timestamp);
    
    return chain->add_version(new_version);
}

bool MVCCManager::remove(const std::string& key, uint64_t transaction_id, 
                        uint64_t delete_timestamp) {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    
    auto it = version_chains_.find(key);
    if (it == version_chains_.end()) {
        return false;
    }
    
    // 获取最新版本并标记删除
    VersionedValue* latest = it->second->get_latest_version();
    if (latest) {
        return it->second->mark_deleted(latest->version, delete_timestamp);
    }
    
    return false;
}

void MVCCManager::commit_transaction(uint64_t transaction_id, uint64_t commit_timestamp) {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    
    // 将该事务创建的所有版本标记为已提交
    for (const auto& pair : version_chains_) {
        auto versions = pair.second->get_all_versions();
        for (auto* version : versions) {
            if (version->transaction_id == transaction_id) {
                version->is_committed = true;
            }
        }
    }
    
    unregister_active_transaction(transaction_id);
    
    std::cout << "事务 " << transaction_id << " 的MVCC版本已提交" << std::endl;
}

void MVCCManager::abort_transaction(uint64_t transaction_id) {
    std::unique_lock<std::shared_mutex> lock(data_mutex_);
    
    // 移除该事务创建的所有未提交版本
    for (auto& pair : version_chains_) {
        auto& chain = pair.second;
        // 这里需要实现移除未提交版本的逻辑
        // 简化实现：标记为已中止
    }
    
    unregister_active_transaction(transaction_id);
    
    std::cout << "事务 " << transaction_id << " 的MVCC版本已中止" << std::endl;
}

std::unordered_map<std::string, std::string> MVCCManager::create_snapshot(uint64_t snapshot_timestamp) {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    
    std::unordered_map<std::string, std::string> snapshot;
    
    for (const auto& pair : version_chains_) {
        const std::string& key = pair.first;
        std::string value;
        
        if (read_from_snapshot(key, snapshot_timestamp, value)) {
            snapshot[key] = value;
        }
    }
    
    return snapshot;
}

bool MVCCManager::read_from_snapshot(const std::string& key, uint64_t snapshot_timestamp,
                                    std::string& value) {
    return read(key, snapshot_timestamp, value);
}

size_t MVCCManager::get_total_versions() const {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    
    size_t total = 0;
    for (const auto& pair : version_chains_) {
        total += pair.second->get_version_count();
    }
    
    return total;
}

size_t MVCCManager::get_version_count(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    
    auto it = version_chains_.find(key);
    if (it != version_chains_.end()) {
        return it->second->get_version_count();
    }
    
    return 0;
}

std::vector<std::string> MVCCManager::get_all_keys() const {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    
    std::vector<std::string> keys;
    for (const auto& pair : version_chains_) {
        keys.push_back(pair.first);
    }
    
    return keys;
}

void MVCCManager::start_garbage_collection() {
    if (gc_running_.load()) {
        return;
    }
    
    gc_running_.store(true);
    gc_thread_ = std::thread(&MVCCManager::garbage_collection_worker, this);
    
    std::cout << "MVCC垃圾回收已启动" << std::endl;
}

void MVCCManager::stop_garbage_collection() {
    gc_running_.store(false);
    
    if (gc_thread_.joinable()) {
        gc_thread_.join();
    }
    
    std::cout << "MVCC垃圾回收已停止" << std::endl;
}

size_t MVCCManager::run_garbage_collection() {
    std::unique_lock<std::shared_mutex> lock(data_mutex_);
    
    uint64_t min_active_ts = get_min_active_timestamp();
    size_t total_cleaned = 0;
    
    for (auto& pair : version_chains_) {
        total_cleaned += pair.second->cleanup_old_versions(min_active_ts);
    }
    
    versions_cleaned_.fetch_add(total_cleaned);
    gc_runs_.fetch_add(1);
    
    std::cout << "垃圾回收完成，清理了 " << total_cleaned << " 个版本" << std::endl;
    
    return total_cleaned;
}

void MVCCManager::set_gc_interval(std::chrono::milliseconds interval) {
    gc_interval_ = interval;
}

MVCCManager::MVCCStats MVCCManager::get_statistics() const {
    MVCCStats stats;
    
    {
        std::shared_lock<std::shared_mutex> lock(data_mutex_);
        stats.total_keys = version_chains_.size();
        stats.total_versions = get_total_versions();
    }
    
    {
        std::lock_guard<std::mutex> lock(active_transactions_mutex_);
        stats.active_transactions = active_transactions_.size();
        stats.oldest_active_timestamp = get_min_active_timestamp();
    }
    
    stats.latest_timestamp = global_timestamp_.load();
    stats.gc_runs = gc_runs_.load();
    stats.versions_cleaned = versions_cleaned_.load();
    
    return stats;
}

void MVCCManager::print_statistics() const {
    auto stats = get_statistics();
    
    std::cout << "=== MVCC统计信息 ===" << std::endl;
    std::cout << "总键数: " << stats.total_keys << std::endl;
    std::cout << "总版本数: " << stats.total_versions << std::endl;
    std::cout << "活跃事务数: " << stats.active_transactions << std::endl;
    std::cout << "最老活跃时间戳: " << stats.oldest_active_timestamp << std::endl;
    std::cout << "最新时间戳: " << stats.latest_timestamp << std::endl;
    std::cout << "垃圾回收次数: " << stats.gc_runs << std::endl;
    std::cout << "清理版本数: " << stats.versions_cleaned << std::endl;
    std::cout << "===================" << std::endl;
}

void MVCCManager::set_max_versions_per_key(size_t max_versions) {
    max_versions_per_key_ = max_versions;
}

void MVCCManager::set_version_retention_time(std::chrono::milliseconds retention_time) {
    version_retention_time_ = retention_time;
}

// 私有方法实现
VersionChain* MVCCManager::get_or_create_version_chain(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(data_mutex_);
    
    auto it = version_chains_.find(key);
    if (it != version_chains_.end()) {
        return it->second.get();
    }
    
    auto chain = std::make_unique<VersionChain>(key);
    VersionChain* chain_ptr = chain.get();
    version_chains_[key] = std::move(chain);
    
    return chain_ptr;
}

uint64_t MVCCManager::get_min_active_timestamp() const {
    std::lock_guard<std::mutex> lock(active_transactions_mutex_);
    
    if (active_transactions_.empty()) {
        return global_timestamp_.load();
    }
    
    uint64_t min_ts = UINT64_MAX;
    for (const auto& pair : active_transactions_) {
        min_ts = std::min(min_ts, pair.second);
    }
    
    return min_ts;
}

void MVCCManager::garbage_collection_worker() {
    while (gc_running_.load()) {
        std::this_thread::sleep_for(gc_interval_);
        
        if (gc_running_.load()) {
            run_garbage_collection();
        }
    }
}

uint64_t MVCCManager::generate_timestamp() {
    return global_timestamp_.fetch_add(1);
}

void MVCCManager::register_active_transaction(uint64_t transaction_id, uint64_t start_timestamp) {
    std::lock_guard<std::mutex> lock(active_transactions_mutex_);
    active_transactions_[transaction_id] = start_timestamp;
}

void MVCCManager::unregister_active_transaction(uint64_t transaction_id) {
    std::lock_guard<std::mutex> lock(active_transactions_mutex_);
    active_transactions_.erase(transaction_id);
}