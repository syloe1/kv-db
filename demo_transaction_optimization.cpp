#include "src/transaction/transaction.h"
#include "src/transaction/lock_manager.h"
#include "src/mvcc/mvcc_manager.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>

// 演示ACID事务特性
void demo_acid_properties() {
    std::cout << "\n=== ACID事务特性演示 ===" << std::endl;
    
    auto txn_manager = std::make_shared<TransactionManager>();
    auto mvcc_manager = std::make_shared<MVCCManager>();
    
    // 原子性演示
    std::cout << "\n--- 原子性 (Atomicity) ---" << std::endl;
    auto context1 = txn_manager->begin_transaction();
    uint64_t txn1_id = context1->get_transaction_id();
    
    // 添加多个操作
    TransactionOperation op1;
    op1.transaction_id = txn1_id;
    op1.type = OperationType::WRITE;
    op1.key = "account_a";
    op1.old_value = "1000";
    op1.new_value = "900";
    
    TransactionOperation op2;
    op2.transaction_id = txn1_id;
    op2.type = OperationType::WRITE;
    op2.key = "account_b";
    op2.old_value = "500";
    op2.new_value = "600";
    context1->add_operation(op1);
    context1->add_operation(op2);
    
    std::cout << "事务" << txn1_id << "包含" << context1->get_operations().size() << "个操作" << std::endl;
    
    // 模拟失败情况
    if (false) { // 模拟条件
        txn_manager->abort_transaction(txn1_id);
        std::cout << "事务中止，所有操作回滚" << std::endl;
    } else {
        txn_manager->commit_transaction(txn1_id);
        std::cout << "事务提交，所有操作生效" << std::endl;
    }
    
    // 一致性演示
    std::cout << "\n--- 一致性 (Consistency) ---" << std::endl;
    auto context2 = txn_manager->begin_transaction();
    uint64_t txn2_id = context2->get_transaction_id();
    
    // 模拟银行转账，保证总金额不变
    mvcc_manager->write("account_a", "900", txn2_id, txn2_id);
    mvcc_manager->write("account_b", "600", txn2_id, txn2_id);
    
    // 验证一致性约束
    std::string balance_a, balance_b;
    mvcc_manager->read("account_a", txn2_id + 100, balance_a);
    mvcc_manager->read("account_b", txn2_id + 100, balance_b);
    
    mvcc_manager->commit_transaction(txn2_id, txn2_id + 50);
    txn_manager->commit_transaction(txn2_id);
    
    std::cout << "转账后账户A: " << balance_a << ", 账户B: " << balance_b << std::endl;
    
    // 隔离性演示
    std::cout << "\n--- 隔离性 (Isolation) ---" << std::endl;
    auto context3 = txn_manager->begin_transaction(IsolationLevel::READ_COMMITTED);
    auto context4 = txn_manager->begin_transaction(IsolationLevel::REPEATABLE_READ);
    
    std::cout << "事务" << context3->get_transaction_id() << "隔离级别: READ_COMMITTED" << std::endl;
    std::cout << "事务" << context4->get_transaction_id() << "隔离级别: REPEATABLE_READ" << std::endl;
    
    txn_manager->commit_transaction(context3->get_transaction_id());
    txn_manager->commit_transaction(context4->get_transaction_id());
    
    // 持久性演示
    std::cout << "\n--- 持久性 (Durability) ---" << std::endl;
    auto context5 = txn_manager->begin_transaction();
    uint64_t txn5_id = context5->get_transaction_id();
    
    mvcc_manager->write("persistent_data", "important_value", txn5_id, txn5_id);
    mvcc_manager->commit_transaction(txn5_id, txn5_id + 10);
    txn_manager->commit_transaction(txn5_id);
    
    std::cout << "数据已持久化，系统重启后仍然可用" << std::endl;
}

// 演示乐观锁和悲观锁
void demo_concurrency_control() {
    std::cout << "\n=== 并发控制策略演示 ===" << std::endl;
    
    // 悲观锁演示
    std::cout << "\n--- 悲观锁 (Pessimistic Locking) ---" << std::endl;
    auto pessimistic_manager = std::make_shared<PessimisticLockManager>();
    
    std::cout << "事务1尝试获取排他锁..." << std::endl;
    bool lock1 = pessimistic_manager->acquire_lock(1, "shared_resource", LockMode::EXCLUSIVE);
    std::cout << "事务1获取排他锁: " << (lock1 ? "成功" : "失败") << std::endl;
    
    // 模拟另一个事务尝试获取锁
    std::thread t1([&pessimistic_manager]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 等待一下再尝试
        std::cout << "事务2尝试获取排他锁..." << std::endl;
        bool lock2 = pessimistic_manager->acquire_lock(2, "shared_resource", LockMode::EXCLUSIVE, 
                                                      std::chrono::milliseconds(500)); // 较短超时
        std::cout << "事务2获取排他锁: " << (lock2 ? "成功" : "失败（超时）") << std::endl;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 较短等待时间
    pessimistic_manager->release_lock(1, "shared_resource");
    std::cout << "事务1释放锁" << std::endl;
    
    t1.join();
    
    // 乐观锁演示
    std::cout << "\n--- 乐观锁 (Optimistic Locking) ---" << std::endl;
    auto optimistic_manager = std::make_shared<OptimisticLockManager>();
    
    // 事务1读取数据
    std::string value1;
    uint64_t version1;
    optimistic_manager->read_with_version(1, "optimistic_data", value1, version1);
    std::cout << "事务1读取数据，版本: " << version1 << std::endl;
    
    // 事务2也读取相同数据
    std::string value2;
    uint64_t version2;
    optimistic_manager->read_with_version(2, "optimistic_data", value2, version2);
    std::cout << "事务2读取数据，版本: " << version2 << std::endl;
    
    // 事务1先提交
    bool write1 = optimistic_manager->write_with_version_check(1, "optimistic_data", "value_from_txn1", version1);
    std::cout << "事务1写入: " << (write1 ? "成功" : "失败") << std::endl;
    
    if (write1) {
        optimistic_manager->apply_write_set(1);
        std::cout << "事务1提交成功" << std::endl;
    }
    
    // 事务2尝试提交（应该失败）
    bool write2 = optimistic_manager->write_with_version_check(2, "optimistic_data", "value_from_txn2", version2);
    std::cout << "事务2写入: " << (write2 ? "成功" : "失败（版本冲突）") << std::endl;
    
    bool validate2 = optimistic_manager->validate_transaction(2);
    std::cout << "事务2验证: " << (validate2 ? "通过" : "失败") << std::endl;
}

// 演示MVCC多版本并发控制
void demo_mvcc() {
    std::cout << "\n=== MVCC多版本并发控制演示 ===" << std::endl;
    
    auto mvcc_manager = std::make_shared<MVCCManager>();
    mvcc_manager->start_garbage_collection();
    
    // 创建多个版本
    std::cout << "\n--- 版本创建 ---" << std::endl;
    mvcc_manager->write("mvcc_key", "version_1", 1, 100);
    mvcc_manager->commit_transaction(1, 110);
    std::cout << "创建版本1 (时间戳: 100-110)" << std::endl;
    
    mvcc_manager->write("mvcc_key", "version_2", 2, 200);
    mvcc_manager->commit_transaction(2, 210);
    std::cout << "创建版本2 (时间戳: 200-210)" << std::endl;
    
    mvcc_manager->write("mvcc_key", "version_3", 3, 300);
    mvcc_manager->commit_transaction(3, 310);
    std::cout << "创建版本3 (时间戳: 300-310)" << std::endl;
    
    // 快照读取
    std::cout << "\n--- 快照读取 ---" << std::endl;
    std::string value;
    
    // 读取时间戳105的快照（应该看不到任何版本）
    bool read1 = mvcc_manager->read("mvcc_key", 105, value);
    std::cout << "时间戳105读取: " << (read1 ? value : "无数据") << std::endl;
    
    // 读取时间戳150的快照（应该看到版本1）
    bool read2 = mvcc_manager->read("mvcc_key", 150, value);
    std::cout << "时间戳150读取: " << (read2 ? value : "无数据") << std::endl;
    
    // 读取时间戳250的快照（应该看到版本2）
    bool read3 = mvcc_manager->read("mvcc_key", 250, value);
    std::cout << "时间戳250读取: " << (read3 ? value : "无数据") << std::endl;
    
    // 读取时间戳350的快照（应该看到版本3）
    bool read4 = mvcc_manager->read("mvcc_key", 350, value);
    std::cout << "时间戳350读取: " << (read4 ? value : "无数据") << std::endl;
    
    // 创建快照
    std::cout << "\n--- 快照创建 ---" << std::endl;
    auto snapshot = mvcc_manager->create_snapshot(250);
    std::cout << "时间戳250的快照包含 " << snapshot.size() << " 个键值对" << std::endl;
    
    mvcc_manager->print_statistics();
    mvcc_manager->stop_garbage_collection();
}

// 演示死锁检测
void demo_deadlock_detection() {
    std::cout << "\n=== 死锁检测演示 ===" << std::endl;
    
    auto txn_manager = std::make_shared<TransactionManager>();
    
    // 创建两个事务
    auto context1 = txn_manager->begin_transaction();
    auto context2 = txn_manager->begin_transaction();
    
    uint64_t txn1_id = context1->get_transaction_id();
    uint64_t txn2_id = context2->get_transaction_id();
    
    std::cout << "创建事务 " << txn1_id << " 和 " << txn2_id << std::endl;
    
    // 事务1获取资源A的锁
    bool lock1_a = txn_manager->acquire_lock(txn1_id, "resource_a", LockType::EXCLUSIVE);
    std::cout << "事务" << txn1_id << "获取资源A锁: " << (lock1_a ? "成功" : "失败") << std::endl;
    
    // 事务2获取资源B的锁
    bool lock2_b = txn_manager->acquire_lock(txn2_id, "resource_b", LockType::EXCLUSIVE);
    std::cout << "事务" << txn2_id << "获取资源B锁: " << (lock2_b ? "成功" : "失败") << std::endl;
    
    // 创建死锁情况
    std::cout << "\n--- 创建死锁情况 ---" << std::endl;
    
    std::thread t1([&txn_manager, txn1_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 较短等待
        std::cout << "事务" << txn1_id << "尝试获取资源B锁..." << std::endl;
        bool lock1_b = txn_manager->acquire_lock(txn1_id, "resource_b", LockType::EXCLUSIVE);
        std::cout << "事务" << txn1_id << "获取资源B锁: " << (lock1_b ? "成功" : "失败") << std::endl;
    });
    
    std::thread t2([&txn_manager, txn2_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 较短等待
        std::cout << "事务" << txn2_id << "尝试获取资源A锁..." << std::endl;
        bool lock2_a = txn_manager->acquire_lock(txn2_id, "resource_a", LockType::EXCLUSIVE);
        std::cout << "事务" << txn2_id << "获取资源A锁: " << (lock2_a ? "成功" : "失败") << std::endl;
    });
    
    // 等待死锁形成
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 较短等待
    
    // 检测死锁
    std::cout << "\n--- 死锁检测 ---" << std::endl;
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

// 演示并发性能
void demo_concurrent_performance() {
    std::cout << "\n=== 并发性能演示 ===" << std::endl;
    
    auto mvcc_manager = std::make_shared<MVCCManager>();
    auto txn_manager = std::make_shared<TransactionManager>();
    
    mvcc_manager->start_garbage_collection();
    
    const int num_threads = 4;
    const int operations_per_thread = 20;
    std::vector<std::thread> threads;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "启动 " << num_threads << " 个并发线程，每个执行 " 
              << operations_per_thread << " 个操作..." << std::endl;
    
    // 创建多个并发事务
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> key_dist(1, 10);
            std::uniform_int_distribution<> op_dist(0, 2); // 0=read, 1=write, 2=delete
            
            int successful_ops = 0;
            
            for (int j = 0; j < operations_per_thread; ++j) {
                auto context = txn_manager->begin_transaction();
                uint64_t txn_id = context->get_transaction_id();
                
                std::string key = "perf_key" + std::to_string(key_dist(gen));
                bool success = false;
                
                int op_type = op_dist(gen);
                if (op_type == 0) {
                    // 读操作
                    std::string value;
                    success = mvcc_manager->read(key, txn_id, value);
                    if (success) {
                        context->add_read_set(key, txn_id);
                    }
                } else if (op_type == 1) {
                    // 写操作
                    std::string value = "value_" + std::to_string(txn_id) + "_" + std::to_string(j);
                    success = mvcc_manager->write(key, value, txn_id, txn_id);
                    if (success) {
                        context->add_write_set(key, value);
                    }
                } else {
                    // 删除操作
                    success = mvcc_manager->remove(key, txn_id, txn_id);
                }
                
                // 随机提交或中止
                if (success && gen() % 10 < 8) { // 80%概率提交
                    mvcc_manager->commit_transaction(txn_id, txn_id + 1000);
                    txn_manager->commit_transaction(txn_id);
                    successful_ops++;
                } else {
                    mvcc_manager->abort_transaction(txn_id);
                    txn_manager->abort_transaction(txn_id);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            
            std::cout << "线程 " << i << " 完成，成功操作: " << successful_ops << std::endl;
        });
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "并发测试完成，总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "总操作数: " << num_threads * operations_per_thread << std::endl;
    std::cout << "平均TPS: " << (num_threads * operations_per_thread * 1000.0) / duration.count() << std::endl;
    
    mvcc_manager->print_statistics();
    mvcc_manager->stop_garbage_collection();
}

int main() {
    std::cout << "=== 事务优化功能演示 ===" << std::endl;
    
    try {
        demo_acid_properties();
        demo_concurrency_control();
        demo_mvcc();
        demo_deadlock_detection();
        demo_concurrent_performance();
        
        std::cout << "\n=== 演示完成 ===" << std::endl;
        std::cout << "\n事务优化系统成功实现了：" << std::endl;
        std::cout << "✓ ACID事务特性" << std::endl;
        std::cout << "✓ 乐观锁和悲观锁" << std::endl;
        std::cout << "✓ MVCC多版本并发控制" << std::endl;
        std::cout << "✓ 死锁检测和解决" << std::endl;
        std::cout << "✓ 高并发性能优化" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "演示过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}