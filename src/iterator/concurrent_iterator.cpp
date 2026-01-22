#include "iterator/concurrent_iterator.h"
#include <chrono>
#include <thread>

ConcurrentIterator::ConcurrentIterator(std::unique_ptr<Iterator> inner)
    : inner_(std::move(inner)) {
}

void ConcurrentIterator::seek(const std::string& target) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (!invalidated_.load()) {
        inner_->seek(target);
    }
}

void ConcurrentIterator::seek_to_first() {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (!invalidated_.load()) {
        inner_->seek_to_first();
    }
}

void ConcurrentIterator::seek_with_prefix(const std::string& prefix) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (!invalidated_.load()) {
        inner_->seek_with_prefix(prefix);
    }
}

void ConcurrentIterator::next() {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (!invalidated_.load()) {
        inner_->next();
    }
}

bool ConcurrentIterator::valid() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    if (invalidated_.load()) {
        return false;
    }
    return inner_->valid();
}

std::string ConcurrentIterator::key() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    if (invalidated_.load()) {
        return "";
    }
    return inner_->key();
}

std::string ConcurrentIterator::value() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    if (invalidated_.load()) {
        return "";
    }
    return inner_->value();
}

void ConcurrentIterator::acquire_read_lock() {
    ref_count_.fetch_add(1);
}

void ConcurrentIterator::release_read_lock() {
    ref_count_.fetch_sub(1);
}

bool ConcurrentIterator::is_valid_for_reading() const {
    return !invalidated_.load();
}

// IteratorManager 实现
IteratorManager& IteratorManager::instance() {
    static IteratorManager instance;
    return instance;
}

void IteratorManager::register_iterator(std::shared_ptr<ConcurrentIterator> iter) {
    std::lock_guard<std::mutex> lock(manager_mutex_);
    cleanup_expired_iterators();
    active_iterators_.emplace_back(iter);
}

void IteratorManager::unregister_iterator(ConcurrentIterator* iter) {
    std::lock_guard<std::mutex> lock(manager_mutex_);
    active_iterators_.erase(
        std::remove_if(active_iterators_.begin(), active_iterators_.end(),
            [iter](const std::weak_ptr<ConcurrentIterator>& weak_iter) {
                auto shared_iter = weak_iter.lock();
                return !shared_iter || shared_iter.get() == iter;
            }),
        active_iterators_.end());
}

void IteratorManager::invalidate_all_iterators() {
    std::lock_guard<std::mutex> lock(manager_mutex_);
    cleanup_expired_iterators();
    
    for (auto& weak_iter : active_iterators_) {
        if (auto iter = weak_iter.lock()) {
            iter->invalidated_.store(true);
        }
    }
}

void IteratorManager::wait_for_iterators() {
    std::lock_guard<std::mutex> lock(manager_mutex_);
    cleanup_expired_iterators();
    
    // 等待所有迭代器完成当前操作
    for (auto& weak_iter : active_iterators_) {
        if (auto iter = weak_iter.lock()) {
            // 等待引用计数归零
            while (iter->ref_count_.load() > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
    }
}

void IteratorManager::cleanup_expired_iterators() {
    active_iterators_.erase(
        std::remove_if(active_iterators_.begin(), active_iterators_.end(),
            [](const std::weak_ptr<ConcurrentIterator>& weak_iter) {
                return weak_iter.expired();
            }),
        active_iterators_.end());
}