#include "benchmark/ycsb_benchmark.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <map>

YCSBBenchmark::YCSBBenchmark(KVDB& db) : db_(db) {
}

void YCSBBenchmark::set_workload_type(WorkloadType type) {
    workload_type_ = type;
}

void YCSBBenchmark::set_record_count(size_t count) {
    record_count_ = count;
}

void YCSBBenchmark::set_operation_count(size_t count) {
    operation_count_ = count;
}

void YCSBBenchmark::set_thread_count(size_t count) {
    thread_count_ = count;
}

void YCSBBenchmark::set_key_size(size_t size) {
    key_size_ = size;
}

void YCSBBenchmark::set_value_size(size_t size) {
    value_size_ = size;
}

void YCSBBenchmark::set_scan_length(size_t length) {
    scan_length_ = length;
}

void YCSBBenchmark::load_data() {
    std::cout << "Loading " << record_count_ << " records..." << std::endl;
    
    std::mt19937 gen(rd_());
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < record_count_; i++) {
        std::string key = "user" + std::to_string(i);
        std::string value = generate_value(gen);
        db_.put(key, value);
        
        if (i % 1000 == 0) {
            std::cout << "Loaded " << i << " records..." << std::endl;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end_time - start_time).count();
    
    std::cout << "Data loading completed in " << std::fixed << std::setprecision(2) 
              << duration << " seconds" << std::endl;
    std::cout << "Loading throughput: " << std::fixed << std::setprecision(2)
              << record_count_ / duration << " ops/sec" << std::endl;
}

BenchmarkResult YCSBBenchmark::run_benchmark() {
    std::cout << "\n=== Starting YCSB Benchmark ===" << std::endl;
    std::cout << "Workload: ";
    
    switch (workload_type_) {
        case WorkloadType::WORKLOAD_A: std::cout << "A (50% read, 50% update)"; break;
        case WorkloadType::WORKLOAD_B: std::cout << "B (95% read, 5% update)"; break;
        case WorkloadType::WORKLOAD_C: std::cout << "C (100% read)"; break;
        case WorkloadType::WORKLOAD_D: std::cout << "D (95% read, 5% insert)"; break;
        case WorkloadType::WORKLOAD_E: std::cout << "E (95% scan, 5% insert)"; break;
        case WorkloadType::WORKLOAD_F: std::cout << "F (50% read, 50% RMW)"; break;
    }
    
    std::cout << std::endl;
    std::cout << "Operations: " << operation_count_ << std::endl;
    std::cout << "Threads: " << thread_count_ << std::endl;
    std::cout << "Key size: " << key_size_ << " bytes" << std::endl;
    std::cout << "Value size: " << value_size_ << " bytes" << std::endl;
    
    // 清空统计信息
    operation_stats_.clear();
    
    // 启动工作线程
    std::vector<std::thread> threads;
    size_t operations_per_thread = operation_count_ / thread_count_;
    
    auto benchmark_start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < thread_count_; i++) {
        threads.emplace_back(&YCSBBenchmark::worker_thread, this, i, operations_per_thread);
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto benchmark_end = std::chrono::high_resolution_clock::now();
    
    // 分析结果
    BenchmarkResult result = analyze_results();
    result.duration_seconds = std::chrono::duration<double>(benchmark_end - benchmark_start).count();
    result.throughput_ops_per_sec = result.successful_operations / result.duration_seconds;
    
    return result;
}

OperationType YCSBBenchmark::get_next_operation(std::mt19937& gen) {
    std::uniform_int_distribution<int> dist(1, 100);
    int rand_val = dist(gen);
    
    switch (workload_type_) {
        case WorkloadType::WORKLOAD_A:
            return (rand_val <= 50) ? OperationType::READ : OperationType::UPDATE;
            
        case WorkloadType::WORKLOAD_B:
            return (rand_val <= 95) ? OperationType::READ : OperationType::UPDATE;
            
        case WorkloadType::WORKLOAD_C:
            return OperationType::READ;
            
        case WorkloadType::WORKLOAD_D:
            return (rand_val <= 95) ? OperationType::READ : OperationType::INSERT;
            
        case WorkloadType::WORKLOAD_E:
            return (rand_val <= 95) ? OperationType::SCAN : OperationType::INSERT;
            
        case WorkloadType::WORKLOAD_F:
            return (rand_val <= 50) ? OperationType::READ : OperationType::READ_MODIFY_WRITE;
    }
    
    return OperationType::READ;
}

std::string YCSBBenchmark::generate_key(std::mt19937& gen, bool existing) {
    if (existing) {
        std::uniform_int_distribution<size_t> dist(0, record_count_ - 1);
        return "user" + std::to_string(dist(gen));
    } else {
        std::uniform_int_distribution<size_t> dist(record_count_, record_count_ * 2);
        return "user" + std::to_string(dist(gen));
    }
}

std::string YCSBBenchmark::generate_value(std::mt19937& gen) {
    std::uniform_int_distribution<char> char_dist('a', 'z');
    std::string value;
    value.reserve(value_size_);
    
    for (size_t i = 0; i < value_size_; i++) {
        value += char_dist(gen);
    }
    
    return value;
}

bool YCSBBenchmark::execute_read(const std::string& key) {
    std::string value;
    return db_.get(key, value);
}

bool YCSBBenchmark::execute_update(const std::string& key, const std::string& value) {
    return db_.put(key, value);
}

bool YCSBBenchmark::execute_insert(const std::string& key, const std::string& value) {
    return db_.put(key, value);
}

bool YCSBBenchmark::execute_scan(const std::string& start_key) {
    Snapshot snap = db_.get_snapshot();
    auto iter = db_.new_iterator(snap);
    
    size_t count = 0;
    for (iter->seek(start_key); iter->valid() && count < scan_length_; iter->next()) {
        count++;
    }
    
    return count > 0;
}

bool YCSBBenchmark::execute_read_modify_write(const std::string& key) {
    std::string value;
    if (db_.get(key, value)) {
        // 修改值
        value += "_modified";
        return db_.put(key, value);
    }
    return false;
}

void YCSBBenchmark::worker_thread(size_t thread_id, size_t operations_per_thread) {
    std::mt19937 gen(rd_() + thread_id);
    
    for (size_t i = 0; i < operations_per_thread; i++) {
        OperationStat stat;
        stat.type = get_next_operation(gen);
        stat.start_time = std::chrono::high_resolution_clock::now();
        
        std::string key = generate_key(gen, true);
        std::string value = generate_value(gen);
        
        switch (stat.type) {
            case OperationType::READ:
                stat.success = execute_read(key);
                break;
                
            case OperationType::UPDATE:
                stat.success = execute_update(key, value);
                break;
                
            case OperationType::INSERT:
                key = generate_key(gen, false);
                stat.success = execute_insert(key, value);
                break;
                
            case OperationType::SCAN:
                stat.success = execute_scan(key);
                break;
                
            case OperationType::READ_MODIFY_WRITE:
                stat.success = execute_read_modify_write(key);
                break;
        }
        
        stat.end_time = std::chrono::high_resolution_clock::now();
        
        // 记录统计信息
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            operation_stats_.push_back(stat);
        }
    }
}

BenchmarkResult YCSBBenchmark::analyze_results() {
    BenchmarkResult result;
    
    result.total_operations = operation_stats_.size();
    
    std::vector<double> latencies;
    latencies.reserve(operation_stats_.size());
    
    for (const auto& stat : operation_stats_) {
        if (stat.success) {
            result.successful_operations++;
        }
        
        double latency = stat.latency_ms();
        latencies.push_back(latency);
        
        result.operation_counts[stat.type]++;
        result.operation_latencies[stat.type] += latency;
    }
    
    // 计算平均延迟
    if (!latencies.empty()) {
        double total_latency = 0.0;
        for (double latency : latencies) {
            total_latency += latency;
        }
        result.average_latency_ms = total_latency / latencies.size();
        
        // 计算百分位延迟
        std::sort(latencies.begin(), latencies.end());
        size_t p95_index = static_cast<size_t>(latencies.size() * 0.95);
        size_t p99_index = static_cast<size_t>(latencies.size() * 0.99);
        
        if (p95_index < latencies.size()) {
            result.p95_latency_ms = latencies[p95_index];
        }
        if (p99_index < latencies.size()) {
            result.p99_latency_ms = latencies[p99_index];
        }
    }
    
    // 计算每种操作的平均延迟
    for (auto& [op_type, total_latency] : result.operation_latencies) {
        size_t count = result.operation_counts[op_type];
        if (count > 0) {
            total_latency /= count;
        }
    }
    
    return result;
}

void YCSBBenchmark::print_results(const BenchmarkResult& result) {
    std::cout << "\n=== Benchmark Results ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    
    std::cout << "Total Operations: " << result.total_operations << std::endl;
    std::cout << "Successful Operations: " << result.successful_operations << std::endl;
    std::cout << "Success Rate: " << (100.0 * result.successful_operations / result.total_operations) << "%" << std::endl;
    std::cout << "Duration: " << result.duration_seconds << " seconds" << std::endl;
    std::cout << "Throughput: " << result.throughput_ops_per_sec << " ops/sec" << std::endl;
    
    std::cout << "\n--- Latency Statistics ---" << std::endl;
    std::cout << "Average Latency: " << result.average_latency_ms << " ms" << std::endl;
    std::cout << "95th Percentile: " << result.p95_latency_ms << " ms" << std::endl;
    std::cout << "99th Percentile: " << result.p99_latency_ms << " ms" << std::endl;
    
    std::cout << "\n--- Operation Breakdown ---" << std::endl;
    const char* op_names[] = {"READ", "UPDATE", "INSERT", "SCAN", "RMW"};
    OperationType op_types[] = {
        OperationType::READ, OperationType::UPDATE, OperationType::INSERT,
        OperationType::SCAN, OperationType::READ_MODIFY_WRITE
    };
    
    for (int i = 0; i < 5; i++) {
        auto it = result.operation_counts.find(op_types[i]);
        if (it != result.operation_counts.end() && it->second > 0) {
            auto latency_it = result.operation_latencies.find(op_types[i]);
            double avg_latency = (latency_it != result.operation_latencies.end()) ? 
                                latency_it->second : 0.0;
            
            std::cout << op_names[i] << ": " << it->second << " ops, "
                      << "avg latency: " << avg_latency << " ms" << std::endl;
        }
    }
}