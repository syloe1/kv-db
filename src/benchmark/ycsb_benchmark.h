#pragma once
#include "db/kv_db.h"
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <thread>
#include <atomic>
#include <map>

// YCSB 工作负载类型
enum class WorkloadType {
    WORKLOAD_A,  // 50% read, 50% update
    WORKLOAD_B,  // 95% read, 5% update  
    WORKLOAD_C,  // 100% read
    WORKLOAD_D,  // 95% read, 5% insert (read latest)
    WORKLOAD_E,  // 95% scan, 5% insert
    WORKLOAD_F   // 50% read, 50% read-modify-write
};

// 操作类型
enum class OperationType {
    READ,
    UPDATE,
    INSERT,
    SCAN,
    READ_MODIFY_WRITE
};

// 基准测试结果
struct BenchmarkResult {
    size_t total_operations = 0;
    size_t successful_operations = 0;
    double duration_seconds = 0.0;
    double throughput_ops_per_sec = 0.0;
    double average_latency_ms = 0.0;
    double p95_latency_ms = 0.0;
    double p99_latency_ms = 0.0;
    
    // 按操作类型统计
    std::map<OperationType, size_t> operation_counts;
    std::map<OperationType, double> operation_latencies;
};

// 单个操作的统计信息
struct OperationStat {
    OperationType type;
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;
    bool success;
    
    double latency_ms() const {
        auto duration = end_time - start_time;
        return std::chrono::duration<double, std::milli>(duration).count();
    }
};

class YCSBBenchmark {
public:
    explicit YCSBBenchmark(KVDB& db);
    
    // 配置参数
    void set_workload_type(WorkloadType type);
    void set_record_count(size_t count);
    void set_operation_count(size_t count);
    void set_thread_count(size_t count);
    void set_key_size(size_t size);
    void set_value_size(size_t size);
    void set_scan_length(size_t length);
    
    // 执行基准测试
    BenchmarkResult run_benchmark();
    
    // 预加载数据
    void load_data();
    
    // 打印结果
    void print_results(const BenchmarkResult& result);

private:
    KVDB& db_;
    
    // 配置参数
    WorkloadType workload_type_ = WorkloadType::WORKLOAD_A;
    size_t record_count_ = 1000;
    size_t operation_count_ = 1000;
    size_t thread_count_ = 1;
    size_t key_size_ = 10;
    size_t value_size_ = 100;
    size_t scan_length_ = 10;
    
    // 统计信息
    std::vector<OperationStat> operation_stats_;
    std::mutex stats_mutex_;
    
    // 随机数生成
    std::random_device rd_;
    
    // 工作负载生成
    OperationType get_next_operation(std::mt19937& gen);
    std::string generate_key(std::mt19937& gen, bool existing = true);
    std::string generate_value(std::mt19937& gen);
    
    // 操作执行
    bool execute_read(const std::string& key);
    bool execute_update(const std::string& key, const std::string& value);
    bool execute_insert(const std::string& key, const std::string& value);
    bool execute_scan(const std::string& start_key);
    bool execute_read_modify_write(const std::string& key);
    
    // 工作线程
    void worker_thread(size_t thread_id, size_t operations_per_thread);
    
    // 统计分析
    BenchmarkResult analyze_results();
};