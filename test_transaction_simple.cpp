#include "src/transaction/transaction.h"
#include "src/transaction/lock_manager.h"
#include "src/mvcc/mvcc_manager.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

// 简化的测试，只测试基础功能
void test_basic_transaction() {
    std::cout << "\n=== 测试基础事务功能 ===" << std::endl;
    
    auto txn_manager = std::make_shared<TransactionManager>();
    
    // 开始事务
    auto context = txn_manager->begin_transaction(IsolationLevel::READ_COMMITTED);
    std::cout << "开始事务 " << context->get_transaction_id() << std::endl;
    
    // 添加操作
    TransactionOperation op;
    op.transaction_id = context->get_transaction_id();
    op.type = OperationType::WRITE;
    op.key = "test_key";
    op.new_value = "test_value";
    context->add_operation(op);
    
    // 提交事务
    bool committed = txn_manager->commit_transaction(context->get_transaction_id());
    std::cout << "事务提交: " << (committed ? "成功" : "失败") << std::endl;
}

void test_basic_locks() {
    std::cout << "\n=== 测试基础锁功能 ===" << std::endl;
    
    auto pessimistic_manager = std::make_shared<PessimisticLockManager>();
    
    // 测试获取锁
    bool lock1 = pessimistic_manager->acquire_lock(1, "key1", LockMode::SHARED);
    std::cout << "事务1获取共享锁: " << (lock1 ? "成功" : "失败") << std::endl;
    
    bool lock2 = pessimistic_manager->acquire_lock(2, "key1", LockMode::SHARED);
    std::cout << "事务2获取共享锁: " << (lock2 ? "成功" : "失败") << std::endl;
    
    // 释放锁
    pessimistic_manager->release_lock(1, "key1");
    pessimistic_manager->release_lock(2, "key1");
    
    std::cout << "锁测试完成" << std::endl;
}

void test_basic_mvcc() {
    std::cout << "\n=== 测试基础MVCC功能 ===" << std::endl;
    
    auto mvcc_manager = std::make_shared<MVCCManager>();
    
    // 写入数据
    bool write_success = mvcc_manager->write("key1", "value1", 1, 100);
    std::cout << "写入数据: " << (write_success ? "成功" : "失败") << std::endl;
    
    // 提交事务
    mvcc_manager->commit_transaction(1, 110);
    
    // 读取数据
    std::string value;
    bool read_success = mvcc_manager->read("key1", 120, value);
    std::cout << "读取数据: " << (read_success ? "成功" : "失败") 
              << ", 值: " << value << std::endl;
    
    std::cout << "MVCC测试完成" << std::endl;
}

void test_optimistic_locks() {
    std::cout << "\n=== 测试乐观锁功能 ===" << std::endl;
    
    auto optimistic_manager = std::make_shared<OptimisticLockManager>();
    
    std::string value;
    uint64_t version;
    
    // 读取并获取版本
    bool read_result = optimistic_manager->read_with_version(1, "key1", value, version);
    std::cout << "乐观读取: " << (read_result ? "成功" : "失败") 
              << ", 版本: " << version << std::endl;
    
    // 尝试写入
    bool write_result = optimistic_manager->write_with_version_check(1, "key1", "new_value", version);
    std::cout << "乐观写入: " << (write_result ? "成功" : "失败") << std::endl;
    
    // 验证事务
    bool validate_result = optimistic_manager->validate_transaction(1);
    std::cout << "事务验证: " << (validate_result ? "通过" : "失败") << std::endl;
    
    std::cout << "乐观锁测试完成" << std::endl;
}

int main() {
    std::cout << "开始简化事务系统测试" << std::endl;
    
    try {
        test_basic_transaction();
        test_basic_locks();
        test_basic_mvcc();
        test_optimistic_locks();
        
        std::cout << "\n=== 所有测试完成 ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}