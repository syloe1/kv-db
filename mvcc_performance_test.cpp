#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <random>
#include <atomic>

class MVCCPerformanceTester {
public:
    MVCCPerformanceTester() : version_counter_(1) {}
    
    struct Version {
        std::string value;
        uint64_t timestamp;
        bool committed;
        
        Version(const std::string& val, uint64_t ts) 
            : value(val), timestamp(ts), committed(true) {}
    };
    
    void performance_test() {
        std::cout << "MVCC性能测试..." << std::endl;
        
        const int num_keys = 1000;
        const int num_operations = 10000;
        const int num_threads = 4;
        
        // 初始化数据
        for (int i = 0; i < num_keys; i++) {
            std::string key = "key" + std::to_string(i);
            data_[key].emplace_back("initial_value_" + std::to_string(i), 1);
        }
        
        std::atomic<int> completed_operations(0);
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([&, t]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> key_dist(0, num_keys - 1);
                std::uniform_int_distribution<> op_dist(1, 100);
                
                for (int i = 0; i < num_operations / num_threads; i++) {
                    std::string key = "key" + std::to_string(key_dist(gen));
                    uint64_t timestamp = version_counter_.fetch_add(1);
                    
                    if (op_dist(gen) <= 30) { // 30% 读操作
                        read_latest_version(key, timestamp);
                    } else { // 70% 写操作
                        std::string value = "value_" + std::to_string(t) + "_" + std::to_string(i);
                        write_version(key, value, timestamp);
                    }
                    
                    completed_operations.fetch_add(1);
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::cout << "完成 " << completed_operations.load() << " 个操作" << std::endl;
        std::cout << "总耗时: " << duration.count() << "ms" << std::endl;
        std::cout << "平均每操作: " << (duration.count() / double(completed_operations.load())) << "ms" << std::endl;
        std::cout << "吞吐量: " << (completed_operations.load() * 1000.0 / duration.count()) << " ops/sec" << std::endl;
        
        // 统计版本信息
        size_t total_versions = 0;
        for (const auto& pair : data_) {
            total_versions += pair.second.size();
        }
        std::cout << "总版本数: " << total_versions << std::endl;
        std::cout << "平均每键版本数: " << (total_versions / double(data_.size())) << std::endl;
    }

private:
    std::atomic<uint64_t> version_counter_;
    std::unordered_map<std::string, std::vector<Version>> data_;
    std::mutex data_mutex_;
    
    bool read_latest_version(const std::string& key, uint64_t read_timestamp) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        auto it = data_.find(key);
        if (it == data_.end()) return false;
        
        // 找到最新的已提交版本
        for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
            if (rit->committed && rit->timestamp <= read_timestamp) {
                return true;
            }
        }
        
        return false;
    }
    
    void write_version(const std::string& key, const std::string& value, uint64_t timestamp) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        data_[key].emplace_back(value, timestamp);
    }
};

int main() {
    MVCCPerformanceTester tester;
    tester.performance_test();
    
    std::cout << "MVCC性能测试完成" << std::endl;
    return 0;
}
