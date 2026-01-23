#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>

class DeadlockTester {
public:
    DeadlockTester() : deadlock_detected_(false) {}
    
    void test_deadlock_detection() {
        std::cout << "测试死锁检测..." << std::endl;
        
        std::mutex resource1, resource2;
        std::atomic<bool> thread1_started(false), thread2_started(false);
        
        // 事务1：先锁resource1，再锁resource2
        std::thread txn1([&]() {
            std::cout << "事务1: 尝试获取资源1" << std::endl;
            std::lock_guard<std::mutex> lock1(resource1);
            std::cout << "事务1: 获得资源1" << std::endl;
            
            thread1_started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            std::cout << "事务1: 尝试获取资源2" << std::endl;
            // 这里会等待resource2，可能形成死锁
            try {
                std::unique_lock<std::mutex> lock2(resource2, std::try_to_lock);
                if (lock2.owns_lock()) {
                    std::cout << "事务1: 获得资源2" << std::endl;
                } else {
                    std::cout << "事务1: 无法获得资源2，检测到潜在死锁" << std::endl;
                    deadlock_detected_ = true;
                }
            } catch (...) {
                std::cout << "事务1: 异常，可能是死锁" << std::endl;
                deadlock_detected_ = true;
            }
        });
        
        // 事务2：先锁resource2，再锁resource1
        std::thread txn2([&]() {
            std::cout << "事务2: 尝试获取资源2" << std::endl;
            std::lock_guard<std::mutex> lock2(resource2);
            std::cout << "事务2: 获得资源2" << std::endl;
            
            thread2_started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            std::cout << "事务2: 尝试获取资源1" << std::endl;
            try {
                std::unique_lock<std::mutex> lock1(resource1, std::try_to_lock);
                if (lock1.owns_lock()) {
                    std::cout << "事务2: 获得资源1" << std::endl;
                } else {
                    std::cout << "事务2: 无法获得资源1，检测到潜在死锁" << std::endl;
                    deadlock_detected_ = true;
                }
            } catch (...) {
                std::cout << "事务2: 异常，可能是死锁" << std::endl;
                deadlock_detected_ = true;
            }
        });
        
        txn1.join();
        txn2.join();
        
        if (deadlock_detected_) {
            std::cout << "✓ 成功检测到死锁情况" << std::endl;
        } else {
            std::cout << "✓ 未发生死锁，事务正常执行" << std::endl;
        }
    }

private:
    std::atomic<bool> deadlock_detected_;
};

int main() {
    DeadlockTester tester;
    tester.test_deadlock_detection();
    
    std::cout << "死锁检测测试完成" << std::endl;
    return 0;
}
