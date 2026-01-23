#include "coroutine_processor.h"
#include <iostream>
#include <chrono>

// CoroutineScheduler 实现
CoroutineScheduler::CoroutineScheduler(size_t num_threads) {
    std::cout << "[CoroutineScheduler] 初始化协程调度器，线程数: " << num_threads << "\n";
    
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&CoroutineScheduler::worker_thread, this);
    }
}

CoroutineScheduler::~CoroutineScheduler() {
    stop_.store(true);
    condition_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void CoroutineScheduler::worker_thread() {
    stats_.active_threads.fetch_add(1);
    
    while (!stop_.load()) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return !task_queue_.empty() || stop_.load(); });
            
            if (stop_.load()) break;
            
            if (!task_queue_.empty()) {
                task = std::move(task_queue_.front());
                task_queue_.pop();
                stats_.queue_size.store(task_queue_.size());
            }
        }
        
        if (task) {
            try {
                task();
                stats_.completed_tasks.fetch_add(1);
            } catch (const std::exception& e) {
                std::cerr << "[CoroutineScheduler] 任务执行异常: " << e.what() << "\n";
            }
        }
    }
    
    stats_.active_threads.fetch_sub(1);
}

void CoroutineScheduler::wait_all() {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (task_queue_.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void CoroutineScheduler::print_stats() const {
    std::cout << "\n=== 协程调度器统计 ===\n";
    std::cout << "总任务数: " << stats_.total_tasks.load() << "\n";
    std::cout << "已完成任务: " << stats_.completed_tasks.load() << "\n";
    std::cout << "活跃线程数: " << stats_.active_threads.load() << "\n";
    std::cout << "队列大小: " << stats_.queue_size.load() << "\n";
    std::cout << "====================\n\n";
}

// ConcurrentDBOperations 实现
ConcurrentDBOperations::ConcurrentDBOperations(CoroutineScheduler& scheduler)
    : scheduler_(scheduler) {
    std::cout << "[ConcurrentDBOperations] 初始化并发数据库操作\n";
}

Task<bool> ConcurrentDBOperations::async_put(const std::string& key, const std::string& value) {
    stats_.concurrent_writes.fetch_add(1);
    
    // 这里应该调用实际的数据库put操作
    // 为了演示，我们模拟一个异步操作
    co_return true;
}

Task<std::optional<std::string>> ConcurrentDBOperations::async_get(const std::string& key) {
    stats_.concurrent_reads.fetch_add(1);
    
    // 这里应该调用实际的数据库get操作
    // 为了演示，我们模拟一个异步操作
    co_return std::optional<std::string>("mock_value");
}

Task<bool> ConcurrentDBOperations::async_delete(const std::string& key) {
    stats_.concurrent_writes.fetch_add(1);
    
    // 这里应该调用实际的数据库delete操作
    co_return true;
}

Task<std::vector<bool>> ConcurrentDBOperations::async_batch_put(
    const std::vector<std::pair<std::string, std::string>>& operations) {
    
    stats_.batch_operations.fetch_add(1);
    
    std::vector<bool> results;
    results.reserve(operations.size());
    
    for (const auto& [key, value] : operations) {
        // 批量操作可以减少锁竞争
        results.push_back(true);
    }
    
    co_return results;
}

Task<std::vector<std::optional<std::string>>> ConcurrentDBOperations::async_batch_get(
    const std::vector<std::string>& keys) {
    
    stats_.batch_operations.fetch_add(1);
    
    std::vector<std::optional<std::string>> results;
    results.reserve(keys.size());
    
    for (const auto& key : keys) {
        results.push_back(std::optional<std::string>("mock_value_" + key));
    }
    
    co_return results;
}