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

// 传统单锁MemTable
class TraditionalMemTable {
public:
    void put(const std::string& key, const std::string& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        data_[key] = value;
        writes_.fetch_add(1);
    }
    
    bool get(const std::string& key, std::string& value) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it != data_.end()) {
            value = it->second;
            reads_.fetch_add(1);
            return true;
        }
        return false;
    }
    
    void print_stats() const {
        std::cout << "传统MemTable - 读:" << reads_.load() << " 写:" << writes_.load() << "\n";
    }
    
private:
    mutable std::mutex mutex_;
    std::map<std::string, std::string> data_;
    mutable std::atomic<uint64_t> reads_{0};
    mutable std::atomic<uint64_t> writes_{0};
};

// 读写分离MemTable
class ReadWriteSeparatedMemTable {
public:
    void put(const std::string& key, const std::string& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        data_[key] = value;
        writes_.fetch_add(1);
    }
    
    bool get(const std::string& key, std::string& value) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it != data_.end()) {
            value = it->second;
            reads_.fetch_add(1);
            return true;
        }
        return false;
    }
    
    void print_stats() const {
        std::cout << "读写分离MemTable - 读:" << reads_.load() << " 写:" << writes_.load() << "\n";
    }
    
private:
    mutable std::shared_mutex mutex_;
    std::map<std::string, std::string> data_;
    mutable std::atomic<uint64_t> reads_{0};
    mutable std::atomic<uint64_t> writes_{0};
};

// 分段锁MemTable
class SegmentedMemTable {
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
    }
    
    bool get(const std::string& key, std::string& value) const {
        size_t segment_id = get_segment(key);
        std::shared_lock<std::shared_mutex> lock(segments_[segment_id].mutex);
        auto it = segments_[segment_id].data.find(key);
        if (it != segments_[segment_id].data.end()) {
            value = it->second;
            segments_[segment_id].reads.fetch_add(1);
            return true;
        }
        return false;
    }
    
    void print_stats() const {
        uint64_t total_reads = 0, total_writes = 0;
        for (const auto& segment : segments_) {
            total_reads += segment.reads.load();
            total_writes += segment.writes.load();
        }
        std::cout << "分段锁MemTable - 读:" << total_reads << " 写:" << total_writes << "\n";
    }
    
private:
    std::array<Segment, NUM_SEGMENTS> segments_;
    
    size_t get_segment(const std::string& key) const {
        return std::hash<std::string>{}(key) % NUM_SEGMENTS;
    }
};

// 性能测试函数
template<typename MemTableType>
long benchmark_memtable(const std::string& name, int num_threads, int ops_per_thread, double read_ratio) {
    MemTableType memtable;
    std::vector<std::thread> threads;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> ratio_dis(0.0, 1.0);
            std::uniform_int_distribution<> key_dis(0, 9999);
            
            for (int i = 0; i < ops_per_thread; ++i) {
                std::string key = "key_" + std::to_string(key_dis(gen));
                
                if (ratio_dis(gen) < read_ratio) {
                    // 读操作
                    std::string value;
                    memtable.get(key, value);
                } else {
                    // 写操作
                    std::string value = "value_" + std::to_string(t) + "_" + std::to_string(i);
                    memtable.put(key, value);
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << name << " - 耗时:" << duration << "ms, QPS:" 
              << (double)(num_threads * ops_per_thread) / duration * 1000 << "\n";
    memtable.print_stats();
    
    return duration;
}

int main() {
    std::cout << "=== 全面并发性能对比测试 ===\n\n";
    
    const int num_threads = 8;
    const int ops_per_thread = 50000;  // 增加到5万次操作
    const double read_ratio = 0.8;     // 80%读操作，20%写操作
    
    std::cout << "测试配置:\n";
    std::cout << "线程数: " << num_threads << "\n";
    std::cout << "每线程操作数: " << ops_per_thread << "\n";
    std::cout << "读写比例: " << (read_ratio * 100) << "% 读, " << ((1-read_ratio) * 100) << "% 写\n";
    std::cout << "总操作数: " << (num_threads * ops_per_thread) << "\n\n";
    
    // 1. 传统单锁测试
    std::cout << "1. 传统单锁MemTable测试\n";
    auto traditional_time = benchmark_memtable<TraditionalMemTable>(
        "传统单锁", num_threads, ops_per_thread, read_ratio);
    
    std::cout << "\n2. 读写分离MemTable测试\n";
    auto rw_separated_time = benchmark_memtable<ReadWriteSeparatedMemTable>(
        "读写分离", num_threads, ops_per_thread, read_ratio);
    
    std::cout << "\n3. 分段锁MemTable测试\n";
    auto segmented_time = benchmark_memtable<SegmentedMemTable>(
        "分段锁", num_threads, ops_per_thread, read_ratio);
    
    // 性能对比
    std::cout << "\n=== 性能对比结果 ===\n";
    std::cout << "传统单锁: " << traditional_time << " ms (基准)\n";
    std::cout << "读写分离: " << rw_separated_time << " ms (提升 " 
              << (double)traditional_time / rw_separated_time << "x)\n";
    std::cout << "分段锁: " << segmented_time << " ms (提升 " 
              << (double)traditional_time / segmented_time << "x)\n";
    
    // 评估结果
    double rw_improvement = (double)traditional_time / rw_separated_time;
    double segmented_improvement = (double)traditional_time / segmented_time;
    
    std::cout << "\n=== 优化效果评估 ===\n";
    
    if (rw_improvement >= 2.0) {
        std::cout << "✅ 读写分离优化效果显著 (" << rw_improvement << "x)\n";
    } else if (rw_improvement >= 1.2) {
        std::cout << "✅ 读写分离有一定优化效果 (" << rw_improvement << "x)\n";
    } else {
        std::cout << "⚠️ 读写分离优化效果有限 (" << rw_improvement << "x)\n";
    }
    
    if (segmented_improvement >= 3.0) {
        std::cout << "✅ 分段锁优化效果优秀 (" << segmented_improvement << "x)\n";
    } else if (segmented_improvement >= 2.0) {
        std::cout << "✅ 分段锁优化效果良好 (" << segmented_improvement << "x)\n";
    } else {
        std::cout << "⚠️ 分段锁优化效果有限 (" << segmented_improvement << "x)\n";
    }
    
    std::cout << "\n=== 并发优化总结 ===\n";
    std::cout << "✅ 成功实现的优化:\n";
    std::cout << "1. 读写分离: 支持多读者并发访问\n";
    std::cout << "2. 分段锁: 减少锁竞争，提升并发度\n";
    std::cout << "3. 大数据量测试: 体现真实并发优势\n";
    std::cout << "4. 读多写少场景: 符合实际应用模式\n";
    
    return 0;
}