#pragma once
#include <coroutine>
#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <future>
#include <vector>

// 简单的协程任务结构
template<typename T>
struct Task {
    struct promise_type {
        T value;
        std::exception_ptr exception;
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        
        void return_value(T val) { value = std::move(val); }
        void unhandled_exception() { exception = std::current_exception(); }
    };
    
    std::coroutine_handle<promise_type> handle;
    
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Task() { if (handle) handle.destroy(); }
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    Task(Task&& other) noexcept : handle(std::exchange(other.handle, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, {});
        }
        return *this;
    }
    
    T get() {
        if (handle.promise().exception) {
            std::rethrow_exception(handle.promise().exception);
        }
        return std::move(handle.promise().value);
    }
};

// 协程调度器
class CoroutineScheduler {
public:
    CoroutineScheduler(size_t num_threads = std::thread::hardware_concurrency());
    ~CoroutineScheduler();
    
    // 提交协程任务
    template<typename Func>
    auto submit(Func&& func) -> std::future<decltype(func())> {
        using ReturnType = decltype(func());
        
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::forward<Func>(func)
        );
        
        auto future = task->get_future();
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            task_queue_.emplace([task]() { (*task)(); });
        }
        
        condition_.notify_one();
        return future;
    }
    
    // 批量提交任务
    template<typename Func>
    auto submit_batch(const std::vector<Func>& funcs) -> std::vector<std::future<decltype(funcs[0]())>> {
        using ReturnType = decltype(funcs[0]());
        std::vector<std::future<ReturnType>> futures;
        
        for (const auto& func : funcs) {
            futures.push_back(submit(func));
        }
        
        return futures;
    }
    
    // 等待所有任务完成
    void wait_all();
    
    // 获取统计信息
    struct Stats {
        std::atomic<uint64_t> total_tasks{0};
        std::atomic<uint64_t> completed_tasks{0};
        std::atomic<uint64_t> active_threads{0};
        std::atomic<uint64_t> queue_size{0};
    };
    
    const Stats& get_stats() const { return stats_; }
    void print_stats() const;
    
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
    
    mutable Stats stats_;
    
    void worker_thread();
};

// 并发数据库操作协程
class ConcurrentDBOperations {
public:
    ConcurrentDBOperations(CoroutineScheduler& scheduler);
    
    // 协程化的数据库操作
    Task<bool> async_put(const std::string& key, const std::string& value);
    Task<std::optional<std::string>> async_get(const std::string& key);
    Task<bool> async_delete(const std::string& key);
    
    // 批量操作
    Task<std::vector<bool>> async_batch_put(
        const std::vector<std::pair<std::string, std::string>>& operations);
    Task<std::vector<std::optional<std::string>>> async_batch_get(
        const std::vector<std::string>& keys);
    
    // 并发统计
    struct ConcurrentStats {
        std::atomic<uint64_t> concurrent_reads{0};
        std::atomic<uint64_t> concurrent_writes{0};
        std::atomic<uint64_t> batch_operations{0};
        std::atomic<uint64_t> lock_free_operations{0};
    };
    
    const ConcurrentStats& get_stats() const { return stats_; }
    
private:
    CoroutineScheduler& scheduler_;
    mutable ConcurrentStats stats_;
};