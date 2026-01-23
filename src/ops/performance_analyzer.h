#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <atomic>
#include <mutex>
#include <queue>

namespace kvdb {
namespace ops {

// 查询类型
enum class QueryType {
    GET,
    PUT,
    DELETE,
    SCAN,
    TRANSACTION,
    BATCH
};

// 慢查询记录
struct SlowQuery {
    std::string query_id;
    QueryType type;
    std::string key_pattern;
    std::chrono::microseconds duration;
    std::chrono::system_clock::time_point timestamp;
    size_t data_size;
    std::string client_info;
    std::string stack_trace;
    
    // 查询详细信息
    std::unordered_map<std::string, std::string> metadata;
};

// 性能统计信息
struct PerformanceStats {
    // 查询统计
    size_t total_queries = 0;
    size_t slow_queries = 0;
    std::chrono::microseconds avg_latency{0};
    std::chrono::microseconds p50_latency{0};
    std::chrono::microseconds p95_latency{0};
    std::chrono::microseconds p99_latency{0};
    std::chrono::microseconds max_latency{0};
    
    // 吞吐量统计
    double qps = 0.0; // 每秒查询数
    double tps = 0.0; // 每秒事务数
    
    // 资源使用统计
    size_t memory_usage = 0;
    size_t disk_usage = 0;
    double cpu_usage = 0.0;
    size_t cache_hit_rate = 0;
    
    // 错误统计
    size_t error_count = 0;
    size_t timeout_count = 0;
    
    std::chrono::system_clock::time_point last_updated;
};

// 性能分析配置
struct AnalyzerConfig {
    std::chrono::microseconds slow_query_threshold{1000}; // 1ms
    size_t max_slow_queries = 1000;
    size_t stats_window_size = 3600; // 1小时
    bool enable_stack_trace = false;
    bool enable_query_profiling = true;
    std::vector<std::string> monitored_key_patterns;
};

// 查询性能分析器
class PerformanceAnalyzer {
public:
    explicit PerformanceAnalyzer(const AnalyzerConfig& config = {});
    ~PerformanceAnalyzer();

    // 记录查询开始
    std::string start_query(QueryType type, const std::string& key);
    
    // 记录查询结束
    void end_query(const std::string& query_id, 
                   bool success = true,
                   size_t data_size = 0,
                   const std::string& client_info = "");

    // 获取慢查询列表
    std::vector<SlowQuery> get_slow_queries(size_t limit = 100);
    
    // 获取性能统计
    PerformanceStats get_performance_stats();
    
    // 获取指定时间范围的统计
    PerformanceStats get_stats_in_range(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end);
    
    // 分析查询模式
    std::unordered_map<std::string, size_t> analyze_query_patterns();
    
    // 获取热点键
    std::vector<std::pair<std::string, size_t>> get_hot_keys(size_t limit = 10);
    
    // 生成性能报告
    std::string generate_performance_report();
    
    // 清理历史数据
    void cleanup_old_data(std::chrono::hours retention_period = std::chrono::hours(24));
    
    // 配置更新
    void update_config(const AnalyzerConfig& config);
    
    // 导出分析数据
    bool export_analysis_data(const std::string& output_path);

private:
    AnalyzerConfig config_;
    std::mutex mutex_;
    
    // 活跃查询跟踪
    struct ActiveQuery {
        QueryType type;
        std::string key;
        std::chrono::steady_clock::time_point start_time;
        std::string client_info;
    };
    std::unordered_map<std::string, ActiveQuery> active_queries_;
    
    // 慢查询存储
    std::queue<SlowQuery> slow_queries_;
    
    // 性能统计数据
    std::vector<std::chrono::microseconds> latency_samples_;
    std::unordered_map<QueryType, size_t> query_type_counts_;
    std::unordered_map<std::string, size_t> key_access_counts_;
    
    // 时间窗口统计
    struct TimeWindowStats {
        std::chrono::system_clock::time_point timestamp;
        size_t query_count;
        std::chrono::microseconds total_latency;
        size_t error_count;
    };
    std::queue<TimeWindowStats> time_window_stats_;
    
    // 内部方法
    std::string generate_query_id();
    bool is_slow_query(std::chrono::microseconds duration);
    void update_latency_stats(std::chrono::microseconds latency);
    void update_time_window_stats();
    std::string get_stack_trace();
    void cleanup_expired_stats();
    
    // 统计计算
    std::chrono::microseconds calculate_percentile(double percentile);
    double calculate_qps();
    size_t get_memory_usage();
    size_t get_disk_usage();
    double get_cpu_usage();
};

// 查询性能监控器（RAII风格）
class QueryProfiler {
public:
    QueryProfiler(PerformanceAnalyzer* analyzer, 
                  QueryType type, 
                  const std::string& key,
                  const std::string& client_info = "");
    ~QueryProfiler();
    
    void set_data_size(size_t size);
    void set_success(bool success);

private:
    PerformanceAnalyzer* analyzer_;
    std::string query_id_;
    size_t data_size_ = 0;
    bool success_ = true;
};

// 便利宏定义
#define PROFILE_QUERY(analyzer, type, key) \
    QueryProfiler _profiler(analyzer, type, key)

#define PROFILE_QUERY_WITH_CLIENT(analyzer, type, key, client) \
    QueryProfiler _profiler(analyzer, type, key, client)

} // namespace ops
} // namespace kvdb