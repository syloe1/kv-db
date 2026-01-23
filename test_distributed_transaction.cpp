#include "src/distributed_transaction/distributed_transaction_coordinator.h"
#include "src/distributed_transaction/distributed_transaction_participant.h"
#include "src/distributed_transaction/simple_distributed_network.h"
#include "src/transaction/transaction.h"
#include "src/mvcc/mvcc_manager.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>

class DistributedTransactionTest {
public:
    DistributedTransactionTest() {
        // 创建协调者
        coordinator_network_ = std::make_shared<SimpleDistributedTransactionNetwork>("coordinator");
        coordinator_txn_manager_ = std::make_shared<TransactionManager>();
        
        DistributedTransactionConfig config;
        config.default_timeout = std::chrono::milliseconds(10000);
        config.prepare_timeout = std::chrono::milliseconds(5000);
        config.commit_timeout = std::chrono::milliseconds(5000);
        
        coordinator_ = std::make_shared<DistributedTransactionCoordinator>(
            "coordinator", config, coordinator_network_, coordinator_txn_manager_);
        
        // 创建参与者
        create_participants();
        
        // 设置网络连接
        setup_network();
    }
    
    ~DistributedTransactionTest() {
        stop_all();
    }
    
    void create_participants() {
        for (int i = 0; i < 3; i++) {
            std::string participant_id = "participant" + std::to_string(i);
            
            auto network = std::make_shared<SimpleDistributedTransactionNetwork>(participant_id);
            auto txn_manager = std::make_shared<TransactionManager>();
            auto mvcc_manager = std::make_shared<MVCCManager>();
            
            auto participant = std::make_shared<DistributedTransactionParticipant>(
                participant_id, network, txn_manager, mvcc_manager);
            
            participant_networks_.push_back(network);
            participant_txn_managers_.push_back(txn_manager);
            participant_mvcc_managers_.push_back(mvcc_manager);
            participants_.push_back(participant);
        }
    }
    
    void setup_network() {
        // 协调者注册参与者
        for (int i = 0; i < 3; i++) {
            std::string participant_id = "participant" + std::to_string(i);
            coordinator_->register_participant(participant_id, "127.0.0.1", 9091 + i);
            coordinator_network_->add_node(participant_id, "127.0.0.1", 9091 + i);
        }
        
        // 参与者注册协调者
        for (auto& participant : participants_) {
            participant->register_coordinator("coordinator", "127.0.0.1", 9090);
        }
        
        // 参与者之间的网络连接
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                if (i != j) {
                    std::string peer_id = "participant" + std::to_string(j);
                    participant_networks_[i]->add_node(peer_id, "127.0.0.1", 9091 + j);
                }
            }
        }
    }
    
    bool start_all() {
        std::cout << "\n=== Starting Distributed Transaction System ===" << std::endl;
        
        // 启动协调者
        if (!coordinator_->start()) {
            std::cout << "Failed to start coordinator" << std::endl;
            return false;
        }
        
        // 启动参与者
        for (size_t i = 0; i < participants_.size(); i++) {
            if (!participants_[i]->start()) {
                std::cout << "Failed to start participant " << i << std::endl;
                return false;
            }
        }
        
        // 等待一下让所有组件启动完成
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        std::cout << "All components started successfully" << std::endl;
        return true;
    }
    
    void stop_all() {
        std::cout << "\n=== Stopping Distributed Transaction System ===" << std::endl;
        
        coordinator_->stop();
        
        for (auto& participant : participants_) {
            participant->stop();
        }
        
        std::cout << "All components stopped" << std::endl;
    }
    
    void test_simple_distributed_transaction() {
        std::cout << "\n=== Testing Simple Distributed Transaction ===" << std::endl;
        
        // 创建分布式操作
        std::vector<DistributedOperation> operations;
        
        // 在participant0上写入数据
        operations.emplace_back("participant0", "WRITE", "key1", "value1");
        operations.emplace_back("participant0", "WRITE", "key2", "value2");
        
        // 在participant1上写入数据
        operations.emplace_back("participant1", "WRITE", "key3", "value3");
        operations.emplace_back("participant1", "WRITE", "key4", "value4");
        
        // 在participant2上写入数据
        operations.emplace_back("participant2", "WRITE", "key5", "value5");
        
        try {
            // 开始分布式事务
            std::string txn_id = coordinator_->begin_distributed_transaction(operations);
            std::cout << "Started distributed transaction: " << txn_id << std::endl;
            
            // 等待一下
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // 提交事务
            bool success = coordinator_->commit_distributed_transaction(txn_id);
            
            if (success) {
                std::cout << "✓ Distributed transaction committed successfully!" << std::endl;
            } else {
                std::cout << "✗ Distributed transaction commit failed!" << std::endl;
            }
            
            // 等待事务完成
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
        } catch (const std::exception& e) {
            std::cout << "Exception during distributed transaction: " << e.what() << std::endl;
        }
    }
    
    void test_read_after_write() {
        std::cout << "\n=== Testing Read After Write ===" << std::endl;
        
        // 先写入一些数据
        std::vector<DistributedOperation> write_ops;
        write_ops.emplace_back("participant0", "WRITE", "test_key", "test_value");
        write_ops.emplace_back("participant1", "WRITE", "another_key", "another_value");
        
        try {
            std::string write_txn = coordinator_->begin_distributed_transaction(write_ops);
            std::cout << "Write transaction: " << write_txn << std::endl;
            
            if (coordinator_->commit_distributed_transaction(write_txn)) {
                std::cout << "Write transaction committed" << std::endl;
                
                // 等待写入完成
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                // 然后读取数据
                std::vector<DistributedOperation> read_ops;
                read_ops.emplace_back("participant0", "READ", "test_key");
                read_ops.emplace_back("participant1", "READ", "another_key");
                
                std::string read_txn = coordinator_->begin_distributed_transaction(read_ops);
                std::cout << "Read transaction: " << read_txn << std::endl;
                
                if (coordinator_->commit_distributed_transaction(read_txn)) {
                    std::cout << "✓ Read transaction committed successfully!" << std::endl;
                } else {
                    std::cout << "✗ Read transaction failed!" << std::endl;
                }
            }
            
        } catch (const std::exception& e) {
            std::cout << "Exception during read-after-write test: " << e.what() << std::endl;
        }
    }
    
    void test_transaction_abort() {
        std::cout << "\n=== Testing Transaction Abort ===" << std::endl;
        
        std::vector<DistributedOperation> operations;
        operations.emplace_back("participant0", "WRITE", "abort_key1", "abort_value1");
        operations.emplace_back("participant1", "WRITE", "abort_key2", "abort_value2");
        
        try {
            std::string txn_id = coordinator_->begin_distributed_transaction(operations);
            std::cout << "Started transaction for abort test: " << txn_id << std::endl;
            
            // 等待一下
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // 主动中止事务
            bool success = coordinator_->abort_distributed_transaction(txn_id);
            
            if (success) {
                std::cout << "✓ Transaction aborted successfully!" << std::endl;
            } else {
                std::cout << "✗ Transaction abort failed!" << std::endl;
            }
            
        } catch (const std::exception& e) {
            std::cout << "Exception during abort test: " << e.what() << std::endl;
        }
    }
    
    void print_statistics() {
        std::cout << "\n=== System Statistics ===" << std::endl;
        
        // 协调者统计
        auto coord_stats = coordinator_->get_statistics();
        std::cout << "Coordinator Statistics:" << std::endl;
        std::cout << "  Total Transactions: " << coord_stats.total_transactions << std::endl;
        std::cout << "  Active Transactions: " << coord_stats.active_transactions << std::endl;
        std::cout << "  Committed Transactions: " << coord_stats.committed_transactions << std::endl;
        std::cout << "  Aborted Transactions: " << coord_stats.aborted_transactions << std::endl;
        std::cout << "  Success Rate: " << (coord_stats.success_rate * 100) << "%" << std::endl;
        
        // 参与者统计
        for (size_t i = 0; i < participants_.size(); i++) {
            auto part_stats = participants_[i]->get_statistics();
            std::cout << "Participant " << i << " Statistics:" << std::endl;
            std::cout << "  Total Transactions: " << part_stats.total_transactions << std::endl;
            std::cout << "  Prepared Transactions: " << part_stats.prepared_transactions << std::endl;
            std::cout << "  Committed Transactions: " << part_stats.committed_transactions << std::endl;
            std::cout << "  Aborted Transactions: " << part_stats.aborted_transactions << std::endl;
        }
    }
    
    void run_comprehensive_test() {
        std::cout << "========================================" << std::endl;
        std::cout << "    分布式事务系统综合测试" << std::endl;
        std::cout << "========================================" << std::endl;
        
        if (!start_all()) {
            std::cout << "Failed to start system" << std::endl;
            return;
        }
        
        // 运行各种测试
        test_simple_distributed_transaction();
        test_read_after_write();
        test_transaction_abort();
        
        // 等待所有操作完成
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 打印统计信息
        print_statistics();
        
        std::cout << "\n=== Test Completed ===" << std::endl;
    }

private:
    // 协调者
    std::shared_ptr<DistributedTransactionCoordinator> coordinator_;
    std::shared_ptr<SimpleDistributedTransactionNetwork> coordinator_network_;
    std::shared_ptr<TransactionManager> coordinator_txn_manager_;
    
    // 参与者
    std::vector<std::shared_ptr<DistributedTransactionParticipant>> participants_;
    std::vector<std::shared_ptr<SimpleDistributedTransactionNetwork>> participant_networks_;
    std::vector<std::shared_ptr<TransactionManager>> participant_txn_managers_;
    std::vector<std::shared_ptr<MVCCManager>> participant_mvcc_managers_;
};

int main() {
    try {
        DistributedTransactionTest test;
        test.run_comprehensive_test();
        
        std::cout << "\nAll distributed transaction tests completed!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}