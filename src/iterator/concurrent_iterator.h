#pragma once
#include "iterator/iterator.h"
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <vector>
#include <algorithm>

// 并发安全的迭代器包装器
class ConcurrentIterator : public Iterator {
public:
    explicit ConcurrentIterator(std::unique_ptr<Iterator> inner);
    ~ConcurrentIterator() = default;

    // Iterator 接口
    void seek(const std::string& target) override;
    void seek_to_first() override;
    void seek_with_prefix(const std::string& prefix) override;
    void next() override;
    bool valid() const override;
    std::string key() const override;
    std::string value() const override;

    // 并发控制
    void acquire_read_lock();
    void release_read_lock();
    bool is_valid_for_reading() const;

private:
    friend class IteratorManager;  // 允许 IteratorManager 访问私有成员
    
    std::unique_ptr<Iterator> inner_;
    mutable std::shared_mutex rw_mutex_;  // 读写锁
    std::atomic<bool> invalidated_{false}; // 迭代器是否已失效
    std::atomic<int> ref_count_{0};       // 引用计数
};

// 迭代器管理器，负责管理所有活跃的迭代器
class IteratorManager {
public:
    static IteratorManager& instance();
    
    // 注册新的迭代器
    void register_iterator(std::shared_ptr<ConcurrentIterator> iter);
    
    // 注销迭代器
    void unregister_iterator(ConcurrentIterator* iter);
    
    // 在写操作前使所有迭代器失效
    void invalidate_all_iterators();
    
    // 等待所有迭代器完成当前操作
    void wait_for_iterators();

private:
    IteratorManager() = default;
    
    std::mutex manager_mutex_;
    std::vector<std::weak_ptr<ConcurrentIterator>> active_iterators_;
    
    void cleanup_expired_iterators();
};