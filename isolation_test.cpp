#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>

class IsolationTester {
public:
    IsolationTester() : counter_(0) {}
    
    // 读未提交测试
    void test_read_uncommitted() {
        std::cout << "测试读未提交隔离级别..." << std::endl;
        
        std::atomic<bool> transaction_started(false);
        std::atomic<bool> read_completed(false);
        std::string read_value;
        
        // 写事务
        std::thread writer([&]() {
            transaction_started = true;
            // 模拟写入但未提交
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            counter_ = 100;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            // 提交
            counter_ = 200;
        });
        
        // 读事务
        std::thread reader([&]() {
            while (!transaction_started) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
            
            // 在读未提交级别下，可能读到未提交的值
            int value = counter_.load();
            std::cout << "读未提交级别读取到: " << value << std::endl;
            read_completed = true;
        });
        
        writer.join();
        reader.join();
        
        std::cout << "读未提交测试完成" << std::endl;
    }
    
    // 可重复读测试
    void test_repeatable_read() {
        std::cout << "测试可重复读隔离级别..." << std::endl;
        
        counter_ = 300;
        std::vector<int> read_values;
        
        std::thread reader([&]() {
            // 第一次读取
            read_values.push_back(counter_.load());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // 第二次读取（应该和第一次相同）
            read_values.push_back(counter_.load());
        });
        
        std::thread writer([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            counter_ = 400; // 在读事务中间修改值
        });
        
        reader.join();
        writer.join();
        
        std::cout << "第一次读取: " << read_values[0] << std::endl;
        std::cout << "第二次读取: " << read_values[1] << std::endl;
        
        if (read_values[0] == read_values[1]) {
            std::cout << "✓ 可重复读测试通过" << std::endl;
        } else {
            std::cout << "✗ 可重复读测试失败" << std::endl;
        }
    }
    
    // 串行化测试
    void test_serializable() {
        std::cout << "测试串行化隔离级别..." << std::endl;
        
        counter_ = 500;
        std::vector<int> results;
        std::mutex results_mutex;
        
        auto transaction = [&](int increment) {
            int current = counter_.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter_ = current + increment;
            
            std::lock_guard<std::mutex> lock(results_mutex);
            results.push_back(counter_.load());
        };
        
        std::vector<std::thread> threads;
        for (int i = 1; i <= 5; i++) {
            threads.emplace_back(transaction, i * 10);
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        std::cout << "串行化执行结果: ";
        for (int result : results) {
            std::cout << result << " ";
        }
        std::cout << std::endl;
        
        std::cout << "串行化测试完成" << std::endl;
    }

private:
    std::atomic<int> counter_;
};

int main() {
    IsolationTester tester;
    
    tester.test_read_uncommitted();
    std::cout << std::endl;
    
    tester.test_repeatable_read();
    std::cout << std::endl;
    
    tester.test_serializable();
    
    std::cout << "\n事务隔离级别测试完成" << std::endl;
    return 0;
}
