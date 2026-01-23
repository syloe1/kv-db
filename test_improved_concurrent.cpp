#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <map>
#include <string>
#include <array>
#include <random>

// 分段锁优化的MemTable
class SegmentedConcurrentMemTable {
public:
    static const size_t NUM_SEGMENTS = 16;
    
    struct Segment {
        mutable std::shared_mutex mutex;
        std::map<std::string, std::string> data;
        mutable std::atomic<uint64_t> reads{0};
        mutable std::atomic<uint64_t> writes{0};
    };
    
    void put(const std::string& key, const std::string& value) {
        size_t segment_id = get_segment(key);
        std::unique_lock<std::shared_mutex> lock(segments_[segment_id].mutex);
        segments_[segment_id].data[key] = value;
        segments_[segment_id].writes.fetch_add(1);
        total_writes_.fetch_add(1);
    }
    
    bool get(const std::string& key, std::string& value) const {
        size_t segment_id = get_segment(key);
        std::shared_lock<std::shared_mutex> lock(segments_[segment_id].mutex);
        auto it = segments_[segment_id].data.find(key);
        if (it != segments_[segment_id].data.end()) {
            value = it->second;
            segments_[segment_id].reads.fetch_add(1);
            total_reads_.fetch_add(1);
            return true;
        }
        return false;
    }
    
    void print_stats() const {
        std::cout << "=== 分段锁MemTable统计 ===\n";
        std::cout << "总读取: " << total_reads_.load() << "\n";
        std::cout << "总写入: " << total_writes_.load() << "\n";
        
        for (size_t i = 0; i < NUM_SEGMENTS; ++i) {
            std::cout << "段" << i << " - 读:" << segments_[i].reads.load() 
                      << " 写:" << segments_[i].writes.load() << "\n";
        }
        std::cout << "========================\n\n";
    }
    
private:
    std::array<Segment, NUM_SEGMENTS> segments_;
    mutable std::atomic<uint64_t> total_reads_{0};
    mutable std::atomic<uint64_t> total_writes_{0};
    
    size_t get_segment(const std::string& key) const {
        return std::hash<std::string>{}(key) % NUM_SEGMENTS;
    }
};

int main() {
    std::cout << "=== 改进版并发优化测试 ===\n\n";
    
    // 1. 分段锁性能测试
    std::cout << "1. 分段锁性能测试\n";
    
    SegmentedConcurrentMemTable segmented_table;
    const int num_threads = 8;
    const int ops_per_thread = 10000;  // 增加操作数量
    
    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();
    
    // 启动写入线程
    for (int t = 0; t < num_threads / 2; ++t) {
        threads.emplace_back([&, t]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 999);
            
            for (int i = 0; i < ops_per_thread; ++i) {
                // 使用随机key分布到不同段，减少锁竞争
                std::string key = "key_" + std::to_string(t) + "_" + std::to_string(dis(gen));
                std::string value = "value_" + std::to_string(t) + "_" + std::to_string(i);
                segmented_table.put(key, value);
            }
        });
    }
    
    // 启动读取线程
    for (int t = 0; t < num_threads / 2; ++t) {
        threads.emplace_back([&, t]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 999);
            
            for (int i = 0; i < ops_per_thread; ++i) {
                std::string key = "key_" + std::to_string(t) + "_" + std::to_string(dis(gen));
                std::string value;
                segmented_table.get(key, value);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "分段锁测试完成，耗时: " << duration << " ms\n";
    std::cout << "总操作数: " << num_threads * ops_per_thread << "\n";
    std::cout << "QPS: " << (double)(num_threads * ops_per_thread) / duration * 1000 << "\n";
    segmented_table.print_stats();
    
    return 0;
}