#include "lock_manager.h"
#include <iostream>
#include <algorithm>
#include <unordered_set>

// LockTableEntry 实现
bool LockTableEntry::has_conflicting_lock(LockMode requested_mode, uint64_t requesting_txn) const {
    for (const auto& granted_lock : granted_locks) {
        if (granted_lock->transaction_id != requesting_txn) {
            if (!LockManager::is_compatible(granted_lock->lock_mode, requested_mode)) {
                return true;
            }
        }
    }
    return false;
}

bool LockTableEntry::can_grant_lock(LockMode requested_mode, uint64_t requesting_txn) const {
    // 检查是否与已授予的锁冲突
    if (has_conflicting_lock(requested_mode, requesting_txn)) {
        return false;
    }
    
    // 检查等待队列中是否有更高优先级的请求
    // 简化实现：FIFO策略
    return waiting_queue.empty();
}

void LockTableEntry::grant_compatible_locks() {
    while (!waiting_queue.empty()) {
        auto request = waiting_queue.front();
        
        if (can_grant_lock(request->lock_mode, request->transaction_id)) {
            waiting_queue.pop();
            request->granted = true;
            granted_locks.push_back(request);
            entry_cv.notify_all();
        } else {
            break; // 无法授予更多锁
        }
    }
}

// LockManager 基类实现
LockManager::LockManager() {}

LockManager::~LockManager() {}

bool LockManager::is_compatible(LockMode existing_mode, LockMode requested_mode) {
    // 锁兼容性矩阵
    static const bool compatibility_matrix[6][6] = {
        // NONE, SHARED, EXCLUSIVE, IS, IX, SIX
        {true,  true,  true,      true,  true,  true},  // NONE
        {true,  true,  false,     true,  false, false}, // SHARED
        {true,  false, false,     false, false, false}, // EXCLUSIVE
        {true,  true,  false,     true,  true,  true},  // INTENTION_SHARED
        {true,  false, false,     true,  true,  false}, // INTENTION_EXCLUSIVE
        {true,  false, false,     true,  false, false}  // SHARED_INTENTION_EXCLUSIVE
    };
    
    int existing_idx = static_cast<int>(existing_mode);
    int requested_idx = static_cast<int>(requested_mode);
    
    if (existing_idx >= 0 && existing_idx < 6 && requested_idx >= 0 && requested_idx < 6) {
        return compatibility_matrix[existing_idx][requested_idx];
    }
    
    return false;
}

bool LockManager::is_upgrade_compatible(LockMode current_mode, LockMode requested_mode) {
    // 锁升级兼容性检查
    if (current_mode == LockMode::SHARED && requested_mode == LockMode::EXCLUSIVE) {
        return true;
    }
    if (current_mode == LockMode::INTENTION_SHARED && requested_mode == LockMode::INTENTION_EXCLUSIVE) {
        return true;
    }
    if (current_mode == LockMode::INTENTION_SHARED && requested_mode == LockMode::SHARED_INTENTION_EXCLUSIVE) {
        return true;
    }
    
    return false;
}

std::shared_ptr<LockTableEntry> LockManager::get_or_create_lock_entry(const std::string& resource_id) {
    std::unique_lock<std::shared_mutex> lock(lock_table_mutex_);
    
    auto it = lock_table_.find(resource_id);
    if (it != lock_table_.end()) {
        return it->second;
    }
    
    auto entry = std::make_shared<LockTableEntry>(resource_id);
    lock_table_[resource_id] = entry;
    return entry;
}

void LockManager::cleanup_empty_entries() {
    std::unique_lock<std::shared_mutex> lock(lock_table_mutex_);
    
    for (auto it = lock_table_.begin(); it != lock_table_.end();) {
        auto& entry = it->second;
        std::lock_guard<std::mutex> entry_lock(entry->entry_mutex);
        
        if (entry->granted_locks.empty() && entry->waiting_queue.empty()) {
            it = lock_table_.erase(it);
        } else {
            ++it;
        }
    }
}

// PessimisticLockManager 实现
PessimisticLockManager::PessimisticLockManager() 
    : deadlock_detection_enabled_(true),
      default_lock_timeout_(std::chrono::milliseconds(30000)) {
}

PessimisticLockManager::~PessimisticLockManager() {}

bool PessimisticLockManager::acquire_lock(uint64_t transaction_id, const std::string& resource_id,
                                         LockMode lock_mode, std::chrono::milliseconds timeout) {
    auto entry = get_or_create_lock_entry(resource_id);
    auto request = std::make_shared<LockRequest>(transaction_id, resource_id, lock_mode, timeout);
    
    std::unique_lock<std::mutex> entry_lock(entry->entry_mutex);
    
    // 尝试立即获取锁
    if (try_acquire_lock_immediate(request, entry)) {
        // 记录事务持有的锁
        {
            std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
            transaction_locks_[transaction_id].insert(resource_id);
        }
        
        std::cout << "事务 " << transaction_id << " 立即获取锁 " << resource_id 
                  << " (模式: " << static_cast<int>(lock_mode) << ")" << std::endl;
        return true;
    }
    
    // 需要等待，加入等待队列
    entry->waiting_queue.push(request);
    
    // 更新等待图
    if (deadlock_detection_enabled_) {
        auto blocking_txns = get_blocking_transactions(resource_id, lock_mode, transaction_id);
        update_wait_graph(transaction_id, blocking_txns);
    }
    
    std::cout << "事务 " << transaction_id << " 等待锁 " << resource_id 
              << " (模式: " << static_cast<int>(lock_mode) << ")" << std::endl;
    
    // 等待锁
    bool acquired = wait_for_lock(request, entry);
    
    if (acquired) {
        // 记录事务持有的锁
        {
            std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
            transaction_locks_[transaction_id].insert(resource_id);
        }
        
        // 从等待图中移除
        if (deadlock_detection_enabled_) {
            remove_from_wait_graph(transaction_id);
        }
        
        std::cout << "事务 " << transaction_id << " 获取锁 " << resource_id << " 成功" << std::endl;
    } else {
        std::cout << "事务 " << transaction_id << " 获取锁 " << resource_id << " 失败（超时）" << std::endl;
    }
    
    return acquired;
}

bool PessimisticLockManager::release_lock(uint64_t transaction_id, const std::string& resource_id) {
    auto entry = get_or_create_lock_entry(resource_id);
    
    std::unique_lock<std::mutex> entry_lock(entry->entry_mutex);
    
    // 从已授予锁列表中移除
    auto& granted_locks = entry->granted_locks;
    auto it = std::find_if(granted_locks.begin(), granted_locks.end(),
                          [transaction_id](const std::shared_ptr<LockRequest>& request) {
                              return request->transaction_id == transaction_id;
                          });
    
    if (it != granted_locks.end()) {
        granted_locks.erase(it);
        
        // 从事务锁记录中移除
        {
            std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
            auto txn_it = transaction_locks_.find(transaction_id);
            if (txn_it != transaction_locks_.end()) {
                txn_it->second.erase(resource_id);
                if (txn_it->second.empty()) {
                    transaction_locks_.erase(txn_it);
                }
            }
        }
        
        // 尝试授予等待中的锁
        entry->grant_compatible_locks();
        
        std::cout << "事务 " << transaction_id << " 释放锁 " << resource_id << std::endl;
        return true;
    }
    
    return false;
}

void PessimisticLockManager::release_all_locks(uint64_t transaction_id) {
    std::vector<std::string> locked_resources;
    
    // 获取该事务持有的所有锁
    {
        std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
        auto it = transaction_locks_.find(transaction_id);
        if (it != transaction_locks_.end()) {
            locked_resources.assign(it->second.begin(), it->second.end());
            transaction_locks_.erase(it);
        }
    }
    
    // 释放所有锁
    for (const auto& resource_id : locked_resources) {
        auto entry = get_or_create_lock_entry(resource_id);
        std::unique_lock<std::mutex> entry_lock(entry->entry_mutex);
        
        // 从已授予锁列表中移除该事务的所有锁
        auto& granted_locks = entry->granted_locks;
        granted_locks.erase(
            std::remove_if(granted_locks.begin(), granted_locks.end(),
                          [transaction_id](const std::shared_ptr<LockRequest>& request) {
                              return request->transaction_id == transaction_id;
                          }),
            granted_locks.end());
        
        // 尝试授予等待中的锁
        entry->grant_compatible_locks();
    }
    
    // 从等待图中移除
    if (deadlock_detection_enabled_) {
        remove_from_wait_graph(transaction_id);
    }
    
    std::cout << "事务 " << transaction_id << " 释放所有锁 (" << locked_resources.size() << " 个)" << std::endl;
}

bool PessimisticLockManager::has_lock(uint64_t transaction_id, const std::string& resource_id) const {
    std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
    auto it = transaction_locks_.find(transaction_id);
    if (it != transaction_locks_.end()) {
        return it->second.find(resource_id) != it->second.end();
    }
    return false;
}

LockMode PessimisticLockManager::get_lock_mode(uint64_t transaction_id, const std::string& resource_id) const {
    std::shared_lock<std::shared_mutex> lock(lock_table_mutex_);
    
    auto it = lock_table_.find(resource_id);
    if (it != lock_table_.end()) {
        auto& entry = it->second;
        std::lock_guard<std::mutex> entry_lock(entry->entry_mutex);
        
        for (const auto& granted_lock : entry->granted_locks) {
            if (granted_lock->transaction_id == transaction_id) {
                return granted_lock->lock_mode;
            }
        }
    }
    
    return LockMode::NONE;
}

std::vector<std::string> PessimisticLockManager::get_locked_resources(uint64_t transaction_id) const {
    std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
    
    auto it = transaction_locks_.find(transaction_id);
    if (it != transaction_locks_.end()) {
        return std::vector<std::string>(it->second.begin(), it->second.end());
    }
    
    return {};
}

bool PessimisticLockManager::detect_deadlock() {
    if (!deadlock_detection_enabled_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(wait_graph_mutex_);
    
    std::unordered_set<uint64_t> visited;
    std::unordered_set<uint64_t> rec_stack;
    std::vector<uint64_t> cycle;
    
    for (const auto& pair : wait_graph_) {
        if (visited.find(pair.first) == visited.end()) {
            if (dfs_cycle_detection(pair.first, visited, rec_stack, cycle)) {
                std::cout << "检测到死锁，涉及事务: ";
                for (uint64_t txn : cycle) {
                    std::cout << txn << " ";
                }
                std::cout << std::endl;
                return true;
            }
        }
    }
    
    return false;
}

std::vector<uint64_t> PessimisticLockManager::find_deadlock_cycle() {
    if (!deadlock_detection_enabled_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(wait_graph_mutex_);
    
    std::unordered_set<uint64_t> visited;
    std::unordered_set<uint64_t> rec_stack;
    std::vector<uint64_t> cycle;
    
    for (const auto& pair : wait_graph_) {
        if (visited.find(pair.first) == visited.end()) {
            if (dfs_cycle_detection(pair.first, visited, rec_stack, cycle)) {
                return cycle;
            }
        }
    }
    
    return {};
}

LockManager::LockManagerStats PessimisticLockManager::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void PessimisticLockManager::print_statistics() const {
    auto stats = get_statistics();
    
    std::cout << "=== 悲观锁管理器统计 ===" << std::endl;
    std::cout << "总锁数: " << stats.total_locks << std::endl;
    std::cout << "等待请求: " << stats.waiting_requests << std::endl;
    std::cout << "已授予请求: " << stats.granted_requests << std::endl;
    std::cout << "超时请求: " << stats.timeout_requests << std::endl;
    std::cout << "检测到死锁: " << stats.deadlocks_detected << std::endl;
    std::cout << "平均等待时间: " << stats.average_wait_time_ms << " ms" << std::endl;
    std::cout << "========================" << std::endl;
}

// 私有方法实现
bool PessimisticLockManager::try_acquire_lock_immediate(std::shared_ptr<LockRequest> request,
                                                       std::shared_ptr<LockTableEntry> entry) {
    if (entry->can_grant_lock(request->lock_mode, request->transaction_id)) {
        request->granted = true;
        entry->granted_locks.push_back(request);
        return true;
    }
    return false;
}

bool PessimisticLockManager::wait_for_lock(std::shared_ptr<LockRequest> request,
                                          std::shared_ptr<LockTableEntry> entry) {
    auto timeout_time = request->request_time + request->timeout;
    
    std::unique_lock<std::mutex> lock(entry->entry_mutex);
    
    while (!request->granted) {
        auto now = std::chrono::system_clock::now();
        if (now >= timeout_time) {
            // 超时，从等待队列中移除
            std::queue<std::shared_ptr<LockRequest>> temp_queue;
            while (!entry->waiting_queue.empty()) {
                auto waiting_request = entry->waiting_queue.front();
                entry->waiting_queue.pop();
                if (waiting_request != request) {
                    temp_queue.push(waiting_request);
                }
            }
            entry->waiting_queue = temp_queue;
            return false;
        }
        
        // 等待通知或超时
        entry->entry_cv.wait_until(lock, timeout_time);
    }
    
    return true;
}

void PessimisticLockManager::update_wait_graph(uint64_t waiting_txn, 
                                              const std::vector<uint64_t>& blocking_txns) {
    std::lock_guard<std::mutex> lock(wait_graph_mutex_);
    
    auto& waiters = wait_graph_[waiting_txn];
    waiters.clear();
    for (uint64_t blocking_txn : blocking_txns) {
        waiters.insert(blocking_txn);
    }
}

void PessimisticLockManager::remove_from_wait_graph(uint64_t transaction_id) {
    std::lock_guard<std::mutex> lock(wait_graph_mutex_);
    
    // 移除该事务作为等待者的记录
    wait_graph_.erase(transaction_id);
    
    // 移除该事务作为被等待者的记录
    for (auto& pair : wait_graph_) {
        auto& waiters = pair.second;
        waiters.erase(transaction_id);
    }
}

bool PessimisticLockManager::dfs_cycle_detection(uint64_t node,
                                                std::unordered_set<uint64_t>& visited,
                                                std::unordered_set<uint64_t>& rec_stack,
                                                std::vector<uint64_t>& cycle) {
    visited.insert(node);
    rec_stack.insert(node);
    
    auto it = wait_graph_.find(node);
    if (it != wait_graph_.end()) {
        for (uint64_t neighbor : it->second) {
            if (rec_stack.find(neighbor) != rec_stack.end()) {
                // 找到环
                cycle.push_back(neighbor);
                cycle.push_back(node);
                return true;
            }
            
            if (visited.find(neighbor) == visited.end()) {
                if (dfs_cycle_detection(neighbor, visited, rec_stack, cycle)) {
                    if (!cycle.empty() && cycle[0] != node) {
                        cycle.push_back(node);
                    }
                    return true;
                }
            }
        }
    }
    
    rec_stack.erase(node);
    return false;
}

std::vector<uint64_t> PessimisticLockManager::get_blocking_transactions(
    const std::string& resource_id, LockMode requested_mode, uint64_t requesting_txn) {
    
    std::vector<uint64_t> blocking_txns;
    
    std::shared_lock<std::shared_mutex> lock(lock_table_mutex_);
    auto it = lock_table_.find(resource_id);
    if (it != lock_table_.end()) {
        auto& entry = it->second;
        std::lock_guard<std::mutex> entry_lock(entry->entry_mutex);
        
        for (const auto& granted_lock : entry->granted_locks) {
            if (granted_lock->transaction_id != requesting_txn) {
                if (!is_compatible(granted_lock->lock_mode, requested_mode)) {
                    blocking_txns.push_back(granted_lock->transaction_id);
                }
            }
        }
    }
    
    return blocking_txns;
}

// OptimisticLockManager 实现
OptimisticLockManager::OptimisticLockManager() 
    : global_version_counter_(1) {
}

OptimisticLockManager::~OptimisticLockManager() {}

bool OptimisticLockManager::acquire_lock(uint64_t transaction_id, const std::string& resource_id,
                                        LockMode lock_mode, std::chrono::milliseconds timeout) {
    // 乐观锁不需要实际获取锁，只记录访问
    std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
    transaction_locks_[transaction_id].insert(resource_id);
    
    std::cout << "事务 " << transaction_id << " 乐观访问资源 " << resource_id << std::endl;
    return true;
}

bool OptimisticLockManager::release_lock(uint64_t transaction_id, const std::string& resource_id) {
    std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
    
    auto it = transaction_locks_.find(transaction_id);
    if (it != transaction_locks_.end()) {
        it->second.erase(resource_id);
        if (it->second.empty()) {
            transaction_locks_.erase(it);
        }
        return true;
    }
    
    return false;
}

void OptimisticLockManager::release_all_locks(uint64_t transaction_id) {
    std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
    transaction_locks_.erase(transaction_id);
    
    // 清理事务集合
    cleanup_transaction_sets(transaction_id);
    
    std::cout << "事务 " << transaction_id << " 清理乐观锁资源" << std::endl;
}

bool OptimisticLockManager::has_lock(uint64_t transaction_id, const std::string& resource_id) const {
    std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
    auto it = transaction_locks_.find(transaction_id);
    if (it != transaction_locks_.end()) {
        return it->second.find(resource_id) != it->second.end();
    }
    return false;
}

LockMode OptimisticLockManager::get_lock_mode(uint64_t transaction_id, const std::string& resource_id) const {
    // 乐观锁总是返回SHARED模式（概念上）
    return has_lock(transaction_id, resource_id) ? LockMode::SHARED : LockMode::NONE;
}

std::vector<std::string> OptimisticLockManager::get_locked_resources(uint64_t transaction_id) const {
    std::lock_guard<std::mutex> txn_lock(transaction_locks_mutex_);
    
    auto it = transaction_locks_.find(transaction_id);
    if (it != transaction_locks_.end()) {
        return std::vector<std::string>(it->second.begin(), it->second.end());
    }
    
    return {};
}

LockManager::LockManagerStats OptimisticLockManager::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void OptimisticLockManager::print_statistics() const {
    auto stats = get_statistics();
    
    std::cout << "=== 乐观锁管理器统计 ===" << std::endl;
    std::cout << "总访问数: " << stats.total_locks << std::endl;
    std::cout << "活跃事务: " << stats.granted_requests << std::endl;
    std::cout << "版本冲突: " << stats.timeout_requests << std::endl;
    std::cout << "========================" << std::endl;
}

bool OptimisticLockManager::read_with_version(uint64_t transaction_id, const std::string& key,
                                             std::string& value, uint64_t& version) {
    // 获取当前版本
    version = get_current_version(key);
    
    // 添加到读集
    add_to_read_set(transaction_id, key, version);
    
    // 这里应该从实际存储中读取值
    // 简化实现，返回成功
    value = "value_for_" + key; // 占位符
    
    std::cout << "事务 " << transaction_id << " 乐观读取 " << key 
              << " (版本: " << version << ")" << std::endl;
    
    return true;
}

bool OptimisticLockManager::write_with_version_check(uint64_t transaction_id, const std::string& key,
                                                    const std::string& value, uint64_t expected_version) {
    // 检查版本冲突
    if (has_version_conflict(key, expected_version)) {
        std::cout << "事务 " << transaction_id << " 写入 " << key 
                  << " 版本冲突 (期望: " << expected_version 
                  << ", 当前: " << get_current_version(key) << ")" << std::endl;
        return false;
    }
    
    // 添加到写集
    add_to_write_set(transaction_id, key, value);
    
    std::cout << "事务 " << transaction_id << " 乐观写入 " << key 
              << " (期望版本: " << expected_version << ")" << std::endl;
    
    return true;
}

bool OptimisticLockManager::validate_transaction(uint64_t transaction_id) {
    return validate_read_set(transaction_id);
}

uint64_t OptimisticLockManager::get_current_version(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(version_mutex_);
    
    auto it = key_versions_.find(key);
    if (it != key_versions_.end()) {
        return it->second;
    }
    
    return 0; // 新键的初始版本
}

void OptimisticLockManager::increment_version(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(version_mutex_);
    key_versions_[key] = generate_version();
}

void OptimisticLockManager::set_version(const std::string& key, uint64_t version) {
    std::unique_lock<std::shared_mutex> lock(version_mutex_);
    key_versions_[key] = version;
}

void OptimisticLockManager::add_to_read_set(uint64_t transaction_id, const std::string& key, uint64_t version) {
    std::lock_guard<std::mutex> lock(transaction_sets_mutex_);
    
    auto& txn_sets = transaction_sets_[transaction_id];
    txn_sets.read_set[key] = version;
}

void OptimisticLockManager::add_to_write_set(uint64_t transaction_id, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(transaction_sets_mutex_);
    
    auto& txn_sets = transaction_sets_[transaction_id];
    txn_sets.write_set[key] = value;
}

bool OptimisticLockManager::validate_read_set(uint64_t transaction_id) {
    std::lock_guard<std::mutex> lock(transaction_sets_mutex_);
    
    auto it = transaction_sets_.find(transaction_id);
    if (it == transaction_sets_.end()) {
        return true; // 没有读集，验证通过
    }
    
    const auto& read_set = it->second.read_set;
    
    for (const auto& pair : read_set) {
        const std::string& key = pair.first;
        uint64_t expected_version = pair.second;
        
        if (get_current_version(key) != expected_version) {
            std::cout << "事务 " << transaction_id << " 读集验证失败，键 " << key 
                      << " 版本已变更" << std::endl;
            return false;
        }
    }
    
    std::cout << "事务 " << transaction_id << " 读集验证通过" << std::endl;
    return true;
}

bool OptimisticLockManager::apply_write_set(uint64_t transaction_id) {
    std::lock_guard<std::mutex> lock(transaction_sets_mutex_);
    
    auto it = transaction_sets_.find(transaction_id);
    if (it == transaction_sets_.end()) {
        return true; // 没有写集，应用成功
    }
    
    const auto& write_set = it->second.write_set;
    
    // 应用所有写操作并更新版本
    for (const auto& pair : write_set) {
        const std::string& key = pair.first;
        const std::string& value = pair.second;
        
        // 这里应该将值写入实际存储
        // 简化实现，只更新版本
        increment_version(key);
        
        std::cout << "事务 " << transaction_id << " 应用写操作 " << key 
                  << " = " << value << std::endl;
    }
    
    return true;
}

// 私有方法实现
uint64_t OptimisticLockManager::generate_version() {
    return global_version_counter_.fetch_add(1);
}

bool OptimisticLockManager::has_version_conflict(const std::string& key, uint64_t expected_version) {
    return get_current_version(key) != expected_version;
}

void OptimisticLockManager::cleanup_transaction_sets(uint64_t transaction_id) {
    std::lock_guard<std::mutex> lock(transaction_sets_mutex_);
    transaction_sets_.erase(transaction_id);
}

// HybridLockManager 实现
HybridLockManager::HybridLockManager(std::shared_ptr<PessimisticLockManager> pessimistic_manager,
                                   std::shared_ptr<OptimisticLockManager> optimistic_manager)
    : pessimistic_manager_(pessimistic_manager), optimistic_manager_(optimistic_manager),
      default_use_optimistic_(true), adaptive_strategy_enabled_(false), conflict_threshold_(0.1) {
}

HybridLockManager::~HybridLockManager() {}

bool HybridLockManager::acquire_lock(uint64_t transaction_id, const std::string& resource_id,
                                    LockMode lock_mode, std::chrono::milliseconds timeout) {
    auto* manager = get_manager_for_transaction(transaction_id);
    return manager->acquire_lock(transaction_id, resource_id, lock_mode, timeout);
}

bool HybridLockManager::release_lock(uint64_t transaction_id, const std::string& resource_id) {
    auto* manager = get_manager_for_transaction(transaction_id);
    return manager->release_lock(transaction_id, resource_id);
}

void HybridLockManager::release_all_locks(uint64_t transaction_id) {
    // 尝试两个管理器都释放（防止策略切换的情况）
    pessimistic_manager_->release_all_locks(transaction_id);
    optimistic_manager_->release_all_locks(transaction_id);
    
    // 清理策略记录
    {
        std::lock_guard<std::mutex> lock(strategy_mutex_);
        transaction_strategies_.erase(transaction_id);
    }
}

bool HybridLockManager::has_lock(uint64_t transaction_id, const std::string& resource_id) const {
    return pessimistic_manager_->has_lock(transaction_id, resource_id) ||
           optimistic_manager_->has_lock(transaction_id, resource_id);
}

LockMode HybridLockManager::get_lock_mode(uint64_t transaction_id, const std::string& resource_id) const {
    auto mode = pessimistic_manager_->get_lock_mode(transaction_id, resource_id);
    if (mode != LockMode::NONE) {
        return mode;
    }
    return optimistic_manager_->get_lock_mode(transaction_id, resource_id);
}

std::vector<std::string> HybridLockManager::get_locked_resources(uint64_t transaction_id) const {
    auto pessimistic_resources = pessimistic_manager_->get_locked_resources(transaction_id);
    auto optimistic_resources = optimistic_manager_->get_locked_resources(transaction_id);
    
    // 合并结果
    std::unordered_set<std::string> all_resources;
    all_resources.insert(pessimistic_resources.begin(), pessimistic_resources.end());
    all_resources.insert(optimistic_resources.begin(), optimistic_resources.end());
    
    return std::vector<std::string>(all_resources.begin(), all_resources.end());
}

bool HybridLockManager::detect_deadlock() {
    return pessimistic_manager_->detect_deadlock();
}

std::vector<uint64_t> HybridLockManager::find_deadlock_cycle() {
    return pessimistic_manager_->find_deadlock_cycle();
}

LockManager::LockManagerStats HybridLockManager::get_statistics() const {
    auto pessimistic_stats = pessimistic_manager_->get_statistics();
    auto optimistic_stats = optimistic_manager_->get_statistics();
    
    // 合并统计信息
    LockManagerStats combined_stats;
    combined_stats.total_locks = pessimistic_stats.total_locks + optimistic_stats.total_locks;
    combined_stats.waiting_requests = pessimistic_stats.waiting_requests + optimistic_stats.waiting_requests;
    combined_stats.granted_requests = pessimistic_stats.granted_requests + optimistic_stats.granted_requests;
    combined_stats.timeout_requests = pessimistic_stats.timeout_requests + optimistic_stats.timeout_requests;
    combined_stats.deadlocks_detected = pessimistic_stats.deadlocks_detected;
    
    return combined_stats;
}

void HybridLockManager::print_statistics() const {
    std::cout << "=== 混合锁管理器统计 ===" << std::endl;
    std::cout << "悲观锁管理器:" << std::endl;
    pessimistic_manager_->print_statistics();
    std::cout << "乐观锁管理器:" << std::endl;
    optimistic_manager_->print_statistics();
    
    {
        std::lock_guard<std::mutex> lock(conflict_stats_mutex_);
        std::cout << "全局冲突率: " << global_conflict_stats_.conflict_rate << std::endl;
    }
    std::cout << "========================" << std::endl;
}

void HybridLockManager::set_strategy_for_transaction(uint64_t transaction_id, bool use_optimistic) {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    transaction_strategies_[transaction_id] = use_optimistic;
}

void HybridLockManager::set_default_strategy(bool use_optimistic) {
    default_use_optimistic_ = use_optimistic;
}

void HybridLockManager::set_conflict_threshold(double threshold) {
    conflict_threshold_ = threshold;
}

void HybridLockManager::enable_adaptive_strategy(bool enable) {
    adaptive_strategy_enabled_ = enable;
}

void HybridLockManager::update_conflict_statistics(uint64_t transaction_id, bool had_conflict) {
    std::lock_guard<std::mutex> lock(conflict_stats_mutex_);
    global_conflict_stats_.update(had_conflict);
    
    if (adaptive_strategy_enabled_) {
        update_adaptive_strategy();
    }
}

// 私有方法实现
bool HybridLockManager::should_use_optimistic(uint64_t transaction_id) {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    
    auto it = transaction_strategies_.find(transaction_id);
    if (it != transaction_strategies_.end()) {
        return it->second;
    }
    
    return default_use_optimistic_;
}

LockManager* HybridLockManager::get_manager_for_transaction(uint64_t transaction_id) {
    if (should_use_optimistic(transaction_id)) {
        return optimistic_manager_.get();
    } else {
        return pessimistic_manager_.get();
    }
}

void HybridLockManager::update_adaptive_strategy() {
    std::lock_guard<std::mutex> conflict_lock(conflict_stats_mutex_);
    
    if (global_conflict_stats_.conflict_rate > conflict_threshold_) {
        // 冲突率过高，切换到悲观锁
        default_use_optimistic_ = false;
        std::cout << "自适应策略：切换到悲观锁 (冲突率: " 
                  << global_conflict_stats_.conflict_rate << ")" << std::endl;
    } else {
        // 冲突率较低，使用乐观锁
        default_use_optimistic_ = true;
        std::cout << "自适应策略：使用乐观锁 (冲突率: " 
                  << global_conflict_stats_.conflict_rate << ")" << std::endl;
    }
}