#include "src/transaction/enhanced_transaction_manager.h"
#include "src/transaction/lock_manager.h"
#include "src/mvcc/mvcc_manager.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>

// 测试基础锁管理器功能
void test_lock_managers() {
    std::cout << "\n=== 测试锁管理器 ===" << std::endl;
    
    // 测试悲观锁管理器
    std::cout << "\n--- 测试悲观锁管理器 ---" << std::endl;
    auto pessimistic_manager = std::make_shared<PessimisticLockManager>();
    
    // 事务1获取共享锁
    bool result1 = pessimistic_manager->acquire_lock(1, "key1", LockMode::SHARED);
    std::cout << "事务1获取key1共享锁: " << (result1 ? "成功" : "失败") << std::endl;
    
    // 事务2获取共享锁（应该成功）
    bool result2 = pessimistic_manager->acquire_lock(2, "key1", LockMode::SHARED);
    std::cout << "事务2获取key1共享锁: " << (result2 ? "成功" : "失败") << std::endl;
    
    // 事务3尝试获取排他锁（应该失败或等待）
    std::thread t3([&pessimistic_manager]() {
        bool result3 = pessimistic_manager->acquire_lock(3, "key1", LockMode::EXCLUSIVE, 
                                                        std::chrono::milliseconds(1000));
        std::cout << "事务3获取key1排他锁: " << (result3 ? "成功" : "失败") << std::endl;
    });
    
    // 等待一段时间后释放锁
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    pessimistic_manager->release_lock(1, "key1");
    pessimistic_manager->release_lock(2, "key1");
    
    t3.join();
    
    pessimistic_manager->print_statistics();
    
    // 测试乐观锁管理器
    std::cout << "\n--- 测试乐观锁管理器 ---" << std::endl;
    auto optimistic_manager = std::make_shared<OptimisticLockManager>();
    
    std::string value;
    uint64_t version;
    
    // 事务1读取
    bool read_result = optimistic_manager->read_with_version(1, "key1", value, version);
    std::cout << "事务1读取key1: " << (read_result ? "成功" : "失败") 
              << ", 版本: " << version << std::endl;
    
    // 事务2也读取相同键
    uint64_t version2;
    optimistic_manager->read_with_version(2, "key1", value, version2);
    std::cout << "事务2读取key1, 版本: " << version2 << std::endl;
    
    // 事务1尝试写入
    bool write_result1 = optimistic_manager->write_with_version_check(1, "key1", "new_value1", version);
    std::cout << "事务1写入key1: " << (write_result1 ? "成功" : "失败") << std::endl;
    
    // 应用事务1的写集
    if (write_result1) {
        optimistic_manager->apply_write_set(1);
    }
    
    // 事务2尝试写入（应该失败，因为版本已变更）
    bool write_result2 = optimistic_manager->write_with_version_check(2, "key1", "new_value2", version2);
    std::cout << "事务2写入key1: " << (write_result2 ? "成功" : "失败") << std::endl;
    
    // 验证事务2
    bool validate_result = optimistic_manager->validate_transaction(2);
    std::cout << "事务2验证: " << (validate_result ? "通过" : "失败") << std::endl;
    
    optimistic_manager->print_statistics();
    
    // 测试混合锁管理器
    std::cout << "\n--- 测试混合锁管理器 ---" << std::endl;
    auto hybrid_manager = std::make_shared<HybridLockManager>(pessimistic_manager, optimistic_manager);
    
    // 设置事务1使用乐观策略
    hybrid_manager->set_strategy_for_transaction(1, true);
    // 设置事务2使用悲观策略
    hybrid_manager->set_strategy_for_transaction(2, false);
    
    hybrid_manager->acquire_lock(1, "key2", LockMode::SHARED);
    hybrid_manager->acquire_lock(2, "key2", LockMode::SHARED);
    
    hybrid_manager->print_statistics();
    
    hybrid_manager->release_all_locks(1);
    hybrid_manager->release_all_locks(2);
}

// 测试MVCC功能
void test_mvcc() {
    std::cout << "\n=== 测试MVCC ===" << std::endl;
    
    auto mvcc_manager = std::make_shared<MVCCManager>();
    
    // 启动垃圾回收
    mvcc_manager->start_garbage_collection();
    
    // 事务1写入数据
    uint64_t txn1_id = 1;
    uint64_t txn1_start_ts = 100;
    mvcc_manager->write("key1", "value1_v1", txn1_id, txn1_start_ts);
    mvcc_manager->commit_transaction(txn1_id, txn1_start_ts + 10);
    
    // 事务2读取数据
    uint64_t txn2_id = 2;
    uint64_t txn2_start_ts = 105;
    std::string read_value;
    bool read_success = mvcc_manager->read("key1", txn2_start_ts, read_value);
    std::cout << "事务2读取key1: " << (read_success ? "成功" : "失败") 
              << ", 值: " << read_value << std::endl;
    
    // 事务3更新数据
    uint64_t txn3_id = 3;
    uint64_t txn3_start_ts = 120;
    mvcc_manager->write("key1", "value1_v2", txn3_id, txn3_start_ts);
    mvcc_manager->commit_transaction(txn3_id, txn3_start_ts + 10);
    
    // 事务4读取更新后的数据
    uint64_t txn4_id = 4;
    uint64_t txn4_start_ts = 135;
    bool read_success2 = mvcc_manager->read("key1", txn4_start_ts, read_value);
    std::cout << "事务4读取key1: " << (read_success2 ? "成功" : "失败") 
              << ", 值: " << read_value << std::endl;
    
    // 创建快照
    auto snapshot = mvcc_manager->create_snapshot(110);
    std::cout << "快照包含 " << snapshot.size() << " 个键值对" << std::endl;
    
    mvcc_manager->print_statistics();
    
    // 停止垃圾回收
    mvcc_manager->stop_garbage_collection();
}

// 测试事务管理器
void test_transaction_manager() {
    std::cout << "\n=== 测试事务管理器 ===" << std::endl;
    
    auto txn_manager = std::make_shared<TransactionManager>();
    
    // 开始事务
    auto context1 = txn_manager->begin_transaction(IsolationLevel::READ_COMMITTED);
    std::cout << "开始事务 " << context1->get_transaction_id() << std::endl;
    
    auto context2 = txn_manager->begin_transaction(IsolationLevel::REPEATABLE_READ);
    std::cout << "开始事务 " << context2->get_transaction_id() << std::endl;
    
    // 模拟事务操作
    TransactionOperation op1;
    op1.transaction_id = context1->get_transaction_id();
    op1.type = OperationType::WRITE;
    op1.key = "key1";
    op1.new_value = "value1";
    context1->add_operation(op1);
    
    TransactionOperation op2;
    op2.transaction_id = context2->get_transaction_id();
    op2.type = OperationType::READ;
    op2.key = "key1";
    context2->add_operation(op2);
    
    // 获取锁
    bool lock1 = txn_manager->acquire_lock(context1->get_transaction_id(), "key1", LockType::EXCLUSIVE);
    std::cout << "事务1获取key1排他锁: " << (lock1 ? "成功" : "失败") << std::endl;
    
    bool lock2 = txn_manager->acquire_lock(context2->get_transaction_id(), "key1", LockType::SHARED);
    std::cout << "事务2获取key1共享锁: " << (lock2 ? "成功" : "失败") << std::endl;
    
    // 提交事务1
    bool commit1 = txn_manager->commit_transaction(context1->get_transaction_id());
    std::cout << "事务1提交: " << (commit1 ? "成功" : "失败") << std::endl;
    
    // 现在事务2应该能获取锁
    bool lock2_retry = txn_manager->acquire_lock(context2->get_transaction_id(), "key1", LockType::SHARED);
    std::cout << "事务2重试获取key1共享锁: " << (lock2_retry ? "成功" : "失败") << std::endl;
    
    // 提交事务2
    bool commit2 = txn_manager->commit_transaction(context2->get_transaction_id());
    std::cout << "事务2提交: " << (commit2 ? "成功" : "失败") << std::endl;
    
    // 打印统计信息
    std::cout << "活跃事务数: " << txn_manager->get_active_transaction_count() << std::endl;
    std::cout << "总锁数: " << txn_manager->get_total_lock_count() << std::endl;
}

// 测试死锁检测
void test_deadlock_detection() {
    std::cout << "\n=== 测试死锁检测 ===" << std::endl;
    
    auto txn_manager = std::make_shared<TransactionManager>();
    
    // 创建两个事务
    auto context1 = txn_manager->begin_transaction();
    auto context2 = txn_manager->begin_transaction();
    
    uint64_t txn1_id = context1->get_transaction_id();
    uint64_t txn2_id = context2->get_transaction_id();
    
    std::cout << "创建事务 " << txn1_id << " 和 " << txn2_id << std::endl;
    
    // 事务1获取key1的锁
    bool lock1_1 = txn_manager->acquire_lock(txn1_id, "key1", LockType::EXCLUSIVE);
    std::cout << "事务" << txn1_id << "获取key1锁: " << (lock1_1 ? "成功" : "失败") << std::endl;
    
    // 事务2获取key2的锁
    bool lock2_2 = txn_manager->acquire_lock(txn2_id, "key2", LockType::EXCLUSIVE);
    std::cout << "事务" << txn2_id << "获取key2锁: " << (lock2_2 ? "成功" : "失败") << std::endl;
    
    // 创建死锁情况
    std::thread t1([&txn_manager, txn1_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        bool lock1_2 = txn_manager->acquire_lock(txn1_id, "key2", LockType::EXCLUSIVE);
        std::cout << "事务" << txn1_id << "获取key2锁: " << (lock1_2 ? "成功" : "失败") << std::endl;
    });
    
    std::thread t2([&txn_manager, txn2_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        bool lock2_1 = txn_manager->acquire_lock(txn2_id, "key1", LockType::EXCLUSIVE);
        std::cout << "事务" << txn2_id << "获取key1锁: " << (lock2_1 ? "成功" : "失败") << std::endl;
    });
    
    // 等待死锁形成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 检测死锁
    bool deadlock_detected = txn_manager->detect_deadlock();
    std::cout << "死锁检测结果: " << (deadlock_detected ? "检测到死锁" : "无死锁") << std::endl;
    
    if (deadlock_detected) {
        auto cycle = txn_manager->find_deadlock_cycle();
        std::cout << "死锁环包含事务: ";
        for (uint64_t txn : cycle) {
            std::cout << txn << " ";
        }
        std::cout << std::endl;
        
        // 解决死锁
        bool resolved = txn_manager->resolve_deadlock(cycle);
        std::cout << "死锁解决: " << (resolved ? "成功" : "失败") << std::endl;
    }
    
    t1.join();
    t2.join();
    
    // 清理
    txn_manager->abort_transaction(txn1_id);
    txn_manager->abort_transaction(txn2_id);
}

// 并发测试
void test_concurrent_transactions() {
    std::cout << "\n=== 测试并发事务 ===" << std::endl;
    
    auto mvcc_manager = std::make_shared<MVCCManager>();
    auto txn_manager = std::make_shared<TransactionManager>();
    
    mvcc_manager->start_garbage_collection();
    
    const int num_threads = 4;
    const int operations_per_thread = 10;
    std::vector<std::thread> threads;
    
    // 创建多个并发事务
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> key_dist(1, 5);
            std::uniform_int_distribution<> op_dist(0, 1); // 0=read, 1=write
            
            for (int j = 0; j < operations_per_thread; ++j) {
                auto context = txn_manager->begin_transaction();
                uint64_t txn_id = context->get_transaction_id();
                
                std::string key = "key" + std::to_string(key_dist(gen));
                
                if (op_dist(gen) == 0) {
                    // 读操作
                    std::string value;
                    bool success = mvcc_manager->read(key, txn_id, value);
                    if (success) {
                        context->add_read_set(key, txn_id);
                    }
                } else {
                    // 写操作
                    std::string value = "value_" + std::to_string(txn_id) + "_" + std::to_string(j);
                    bool success = mvcc_manager->write(key, value, txn_id, txn_id);
                    if (success) {
                        context->add_write_set(key, value);
                    }
                }
                
                // 随机提交或中止
                if (gen() % 10 < 8) { // 80%概率提交
                    mvcc_manager->commit_transaction(txn_id, txn_id + 1000);
                    txn_manager->commit_transaction(txn_id);
                } else {
                    mvcc_manager->abort_transaction(txn_id);
                    txn_manager->abort_transaction(txn_id);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "并发测试完成" << std::endl;
    mvcc_manager->print_statistics();
    
    mvcc_manager->stop_garbage_collection();
}

int main() {
    std::cout << "开始事务系统优化测试" << std::endl;
    
    try {
        test_lock_managers();
        test_mvcc();
        test_transaction_manager();
        test_deadlock_detection();
        test_concurrent_transactions();
        
        std::cout << "\n=== 所有测试完成 ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}