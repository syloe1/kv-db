#include "transaction.h"
#include <iostream>
#include <algorithm>
#include <unordered_set>

// TransactionContext 实现
TransactionContext::TransactionContext(uint64_t id, IsolationLevel level)
    : transaction_id_(id), state_(TransactionState::ACTIVE), 
      isolation_level_(level), commit_timestamp_(0), snapshot_version_(0) {
    
    start_timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

TransactionContext::~TransactionContext() {
    // 析构时确保所有锁都被释放
}

void TransactionContext::set_state(TransactionState state) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    state_ = state;
}

void TransactionContext::add_operation(const TransactionOperation& op) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    operations_.push_back(op);
}

void TransactionContext::add_read_set(const std::string& key, uint64_t version) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    read_set_[key] = version;
}

void TransactionContext::add_write_set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    write_set_[key] = value;
}

bool TransactionContext::is_in_read_set(const std::string& key) const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return read_set_.find(key) != read_set_.end();
}

bool TransactionContext::is_in_write_set(const std::string& key) const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return write_set_.find(key) != write_set_.end();
}

void TransactionContext::add_lock(const LockInfo& lock) {
    std::lock_guard<std::mutex> guard(context_mutex_);
    locks_.push_back(lock);
}

void TransactionContext::remove_lock(const std::string& resource_id) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    locks_.erase(std::remove_if(locks_.begin(), locks_.end(),
                               [&resource_id](const LockInfo& info) {
                                   return info.resource_id == resource_id;
                               }), locks_.end());
}

// TransactionManager 实现
TransactionManager::TransactionManager() 
    : next_transaction_id_(1), global_timestamp_(1),
      transaction_timeout_(std::chrono::minutes(5)) {
}

TransactionManager::~TransactionManager() {
    // 中止所有活跃事务
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    for (auto& pair : active_transactions_) {
        pair.second->set_state(TransactionState::ABORTED);
    }
}

std::shared_ptr<TransactionContext> TransactionManager::begin_transaction(IsolationLevel level) {
    uint64_t txn_id = generate_transaction_id();
    auto context = std::make_shared<TransactionContext>(txn_id, level);
    
    // 设置快照版本（用于MVCC）
    context->set_snapshot_version(global_timestamp_.load());
    
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    active_transactions_[txn_id] = context;
    
    std::cout << "开始事务 " << txn_id << ", 隔离级别: " << static_cast<int>(level) << std::endl;
    
    return context;
}

bool TransactionManager::commit_transaction(uint64_t transaction_id) {
    std::shared_ptr<TransactionContext> context;
    
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        auto it = active_transactions_.find(transaction_id);
        if (it == active_transactions_.end()) {
            std::cout << "事务 " << transaction_id << " 不存在或已完成" << std::endl;
            return false;
        }
        context = it->second;
    }
    
    if (!context->is_active()) {
        std::cout << "事务 " << transaction_id << " 不在活跃状态" << std::endl;
        return false;
    }
    
    // 设置准备状态
    context->set_state(TransactionState::PREPARING);
    
    // 验证事务（检查冲突等）
    if (!validate_transaction(context)) {
        std::cout << "事务 " << transaction_id << " 验证失败，中止事务" << std::endl;
        abort_transaction(transaction_id);
        return false;
    }
    
    // 分配提交时间戳
    uint64_t commit_timestamp = generate_timestamp();
    context->set_commit_timestamp(commit_timestamp);
    
    // 应用写操作到数据库
    if (!apply_transaction_writes(context)) {
        std::cout << "事务 " << transaction_id << " 写入失败，中止事务" << std::endl;
        abort_transaction(transaction_id);
        return false;
    }
    
    // 提交成功
    context->set_state(TransactionState::COMMITTED);
    
    // 释放所有锁
    release_all_locks(transaction_id);
    
    // 移动到已完成事务列表
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        active_transactions_.erase(transaction_id);
        completed_transactions_[transaction_id] = context;
    }
    
    std::cout << "事务 " << transaction_id << " 提交成功" << std::endl;
    return true;
}

bool TransactionManager::abort_transaction(uint64_t transaction_id) {
    std::shared_ptr<TransactionContext> context;
    
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        auto it = active_transactions_.find(transaction_id);
        if (it == active_transactions_.end()) {
            return false;
        }
        context = it->second;
    }
    
    // 设置中止状态
    context->set_state(TransactionState::ABORTED);
    
    // 释放所有锁
    release_all_locks(transaction_id);
    
    // 回滚操作（如果需要）
    rollback_transaction_operations(context);
    
    // 移动到已完成事务列表
    {
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        active_transactions_.erase(transaction_id);
        completed_transactions_[transaction_id] = context;
    }
    
    std::cout << "事务 " << transaction_id << " 已中止" << std::endl;
    return true;
}

std::shared_ptr<TransactionContext> TransactionManager::get_transaction(uint64_t transaction_id) {
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    
    auto it = active_transactions_.find(transaction_id);
    if (it != active_transactions_.end()) {
        return it->second;
    }
    
    auto it2 = completed_transactions_.find(transaction_id);
    if (it2 != completed_transactions_.end()) {
        return it2->second;
    }
    
    return nullptr;
}

std::vector<uint64_t> TransactionManager::get_active_transactions() const {
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    
    std::vector<uint64_t> result;
    for (const auto& pair : active_transactions_) {
        result.push_back(pair.first);
    }
    
    return result;
}

bool TransactionManager::is_transaction_active(uint64_t transaction_id) const {
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    return active_transactions_.find(transaction_id) != active_transactions_.end();
}

bool TransactionManager::acquire_lock(uint64_t transaction_id, const std::string& resource_id, 
                                     LockType lock_type) {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    // 检查锁兼容性
    auto it = lock_table_.find(resource_id);
    if (it != lock_table_.end()) {
        for (const auto& existing_lock : it->second) {
            if (existing_lock.transaction_id != transaction_id) {
                if (!is_lock_compatible(existing_lock.lock_type, lock_type)) {
                    // 锁冲突，更新等待图
                    update_wait_graph(transaction_id, existing_lock.transaction_id);
                    std::cout << "事务 " << transaction_id << " 等待锁 " << resource_id << std::endl;
                    return false;
                }
            }
        }
    }
    
    // 获取锁
    LockInfo new_lock(transaction_id, lock_type, resource_id);
    lock_table_[resource_id].push_back(new_lock);
    
    // 添加到事务上下文
    auto context = get_transaction(transaction_id);
    if (context) {
        context->add_lock(new_lock);
    }
    
    std::cout << "事务 " << transaction_id << " 获取锁 " << resource_id 
              << " (类型: " << static_cast<int>(lock_type) << ")" << std::endl;
    
    return true;
}

bool TransactionManager::release_lock(uint64_t transaction_id, const std::string& resource_id) {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    auto it = lock_table_.find(resource_id);
    if (it == lock_table_.end()) {
        return false;
    }
    
    // 移除锁
    auto& locks = it->second;
    locks.erase(std::remove_if(locks.begin(), locks.end(),
                              [transaction_id](const LockInfo& info) {
                                  return info.transaction_id == transaction_id;
                              }), locks.end());
    
    if (locks.empty()) {
        lock_table_.erase(it);
    }
    
    // 从事务上下文中移除
    auto context = get_transaction(transaction_id);
    if (context) {
        context->remove_lock(resource_id);
    }
    
    // 从等待图中移除
    remove_from_wait_graph(transaction_id);
    
    std::cout << "事务 " << transaction_id << " 释放锁 " << resource_id << std::endl;
    
    return true;
}

void TransactionManager::release_all_locks(uint64_t transaction_id) {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    // 遍历所有锁表，移除该事务的锁
    for (auto it = lock_table_.begin(); it != lock_table_.end();) {
        auto& locks = it->second;
        locks.erase(std::remove_if(locks.begin(), locks.end(),
                                  [transaction_id](const LockInfo& info) {
                                      return info.transaction_id == transaction_id;
                                  }), locks.end());
        
        if (locks.empty()) {
            it = lock_table_.erase(it);
        } else {
            ++it;
        }
    }
    
    // 从等待图中移除
    remove_from_wait_graph(transaction_id);
    
    std::cout << "事务 " << transaction_id << " 释放所有锁" << std::endl;
}

bool TransactionManager::detect_deadlock() {
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

std::vector<uint64_t> TransactionManager::find_deadlock_cycle() {
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

bool TransactionManager::resolve_deadlock(const std::vector<uint64_t>& cycle) {
    if (cycle.empty()) {
        return false;
    }
    
    // 选择最年轻的事务中止（简单策略）
    uint64_t victim = *std::max_element(cycle.begin(), cycle.end());
    
    std::cout << "解决死锁：中止事务 " << victim << std::endl;
    return abort_transaction(victim);
}

size_t TransactionManager::get_active_transaction_count() const {
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    return active_transactions_.size();
}

size_t TransactionManager::get_total_lock_count() const {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    size_t count = 0;
    for (const auto& pair : lock_table_) {
        count += pair.second.size();
    }
    
    return count;
}

void TransactionManager::cleanup_completed_transactions() {
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(1); // 保留1小时内的已完成事务
    
    for (auto it = completed_transactions_.begin(); it != completed_transactions_.end();) {
        auto commit_time = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(it->second->get_commit_timestamp()));
        
        if (commit_time < cutoff) {
            it = completed_transactions_.erase(it);
        } else {
            ++it;
        }
    }
}

// 私有方法实现
uint64_t TransactionManager::generate_transaction_id() {
    return next_transaction_id_.fetch_add(1);
}

uint64_t TransactionManager::generate_timestamp() {
    return global_timestamp_.fetch_add(1);
}

bool TransactionManager::is_lock_compatible(LockType existing_lock, LockType requested_lock) {
    // 锁兼容性矩阵
    if (existing_lock == LockType::SHARED && requested_lock == LockType::SHARED) {
        return true; // 共享锁之间兼容
    }
    
    return false; // 其他情况都不兼容（简化实现）
}

void TransactionManager::update_wait_graph(uint64_t waiting_txn, uint64_t holding_txn) {
    std::lock_guard<std::mutex> lock(wait_graph_mutex_);
    wait_graph_[waiting_txn].push_back(holding_txn);
}

void TransactionManager::remove_from_wait_graph(uint64_t transaction_id) {
    std::lock_guard<std::mutex> lock(wait_graph_mutex_);
    
    // 移除该事务作为等待者的记录
    wait_graph_.erase(transaction_id);
    
    // 移除该事务作为被等待者的记录
    for (auto& pair : wait_graph_) {
        auto& waiters = pair.second;
        waiters.erase(std::remove(waiters.begin(), waiters.end(), transaction_id), 
                     waiters.end());
    }
}

bool TransactionManager::dfs_cycle_detection(uint64_t node, 
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

// 辅助方法（需要与数据库集成）
bool TransactionManager::validate_transaction(std::shared_ptr<TransactionContext> context) {
    // 这里应该实现事务验证逻辑
    // 例如检查读集是否仍然有效，写写冲突等
    return true; // 简化实现
}

bool TransactionManager::apply_transaction_writes(std::shared_ptr<TransactionContext> context) {
    // 这里应该将事务的写操作应用到数据库
    // 需要与存储引擎集成
    return true; // 简化实现
}

void TransactionManager::rollback_transaction_operations(std::shared_ptr<TransactionContext> context) {
    // 这里应该回滚事务的操作
    // 需要与存储引擎集成
}