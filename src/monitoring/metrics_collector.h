#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <condition_variable>

namespace kvdb {
namespace monitoring {

// 性能指标快照
struct PerformanceMetrics {
    uint64_t qps = 0;           // 每秒查询数
    uint64_t total_requests = 0; // 总请求数
    double avg_latency = 0.0;    // 平均延迟(ms)
    double p99_latency = 0.0;    // P99延迟(ms)
    uint64_t throughput = 0;     // 吞吐量(bytes/s)
    
    // 操作计数器
    uint64_t get_count = 0;
    uint64_t put_count = 0;
    uint64_t delete_count = 0;
    uint64_t scan_count = 0;
};

// 资源指标快照
struct ResourceMetrics {
    double cpu_usage = 0.0;      // CPU使用率(%)
    uint64_t memory_usage = 0;   // 内存使用量(bytes)
    uint64_t disk_usage = 0;     // 磁盘使用量(bytes)
    uint64_t network_in = 0;     // 网络入流量(bytes/s)
    uint64_t network_out = 0;    // 网络出流量(bytes/s)
    
    // 存储相关
    uint64_t memtable_size = 0;
    uint64_t sstable_count = 0;
    uint64_t wal_size = 0;
};

// 业务指标快照
struct BusinessMetrics {
    uint64_t total_keys = 0;     // 总键数量
    uint64_t total_data_size = 0; // 总数据大小
    double growth_rate = 0.0;    // 增长率(%)
    uint64_t active_connections = 0; // 活跃连接数
    
    // 事务相关
    uint64_t transaction_count = 0;
    uint64_t commit_count = 0;
    uint64_t abort_count = 0;
};

// 告警级别
enum class AlertLevel {
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

// 告警信息
struct Alert {
    AlertLevel level;
    std::string metric_name;
    std::string message;
    double threshold;
    double current_value;
    std::chrono::system_clock::time_point timestamp;
};

// 延迟统计
class LatencyStats {
private:
    std::vector<double> samples_;
    std::mutex mutex_;
    size_t max_samples_;
    
public:
    explicit LatencyStats(size_t max_samples = 10000);
    
    void record(double latency_ms);
    double get_average() const;
    double get_percentile(double p) const;
    void clear();
};

// 指标收集器
class MetricsCollector {
private:
    // 内部使用atomic类型存储
    std::atomic<uint64_t> qps_{0};
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<double> avg_latency_{0.0};
    std::atomic<double> p99_latency_{0.0};
    std::atomic<uint64_t> throughput_{0};
    std::atomic<uint64_t> get_count_{0};
    std::atomic<uint64_t> put_count_{0};
    std::atomic<uint64_t> delete_count_{0};
    std::atomic<uint64_t> scan_count_{0};
    
    std::atomic<double> cpu_usage_{0.0};
    std::atomic<uint64_t> memory_usage_{0};
    std::atomic<uint64_t> disk_usage_{0};
    std::atomic<uint64_t> network_in_{0};
    std::atomic<uint64_t> network_out_{0};
    std::atomic<uint64_t> memtable_size_{0};
    std::atomic<uint64_t> sstable_count_{0};
    std::atomic<uint64_t> wal_size_{0};
    
    std::atomic<uint64_t> total_keys_{0};
    std::atomic<uint64_t> total_data_size_{0};
    std::atomic<double> growth_rate_{0.0};
    std::atomic<uint64_t> active_connections_{0};
    std::atomic<uint64_t> transaction_count_{0};
    std::atomic<uint64_t> commit_count_{0};
    std::atomic<uint64_t> abort_count_{0};
    
    std::unique_ptr<LatencyStats> latency_stats_;
    
    // 告警相关
    std::vector<Alert> alerts_;
    std::mutex alerts_mutex_;
    
    // 监控线程
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    
    // 阈值配置
    struct Thresholds {
        double cpu_warning = 70.0;
        double cpu_critical = 90.0;
        double memory_warning = 80.0;
        double memory_critical = 95.0;
        double latency_warning = 100.0;  // ms
        double latency_critical = 500.0; // ms
        uint64_t qps_warning = 1000;
        uint64_t qps_critical = 5000;
    } thresholds_;
    
    void monitor_loop();
    void check_thresholds();
    void add_alert(AlertLevel level, const std::string& metric, 
                   const std::string& message, double threshold, double value);
    
    // 系统资源监控
    double get_cpu_usage();
    uint64_t get_memory_usage();
    uint64_t get_disk_usage();
    
public:
    MetricsCollector();
    ~MetricsCollector();
    
    // 启动/停止监控
    void start();
    void stop();
    
    // 性能指标记录
    void record_request();
    void record_latency(double latency_ms);
    void record_operation(const std::string& op_type);
    void record_throughput(uint64_t bytes);
    
    // 业务指标更新
    void update_key_count(uint64_t count);
    void update_data_size(uint64_t size);
    void update_connections(int delta);
    void record_transaction(bool committed);
    
    // 获取指标 - 返回快照而不是引用
    PerformanceMetrics get_performance_metrics() const;
    ResourceMetrics get_resource_metrics() const;
    BusinessMetrics get_business_metrics() const;
    
    // 获取告警
    std::vector<Alert> get_alerts() const;
    void clear_alerts();
    
    // 配置阈值
    void set_thresholds(const Thresholds& thresholds);
    
    // 导出指标
    std::string export_prometheus() const;
    std::string export_json() const;
};

// 全局指标收集器实例
extern std::unique_ptr<MetricsCollector> g_metrics_collector;

// 便捷宏
#define RECORD_REQUEST() \
    if (g_metrics_collector) g_metrics_collector->record_request()

#define RECORD_LATENCY(latency) \
    if (g_metrics_collector) g_metrics_collector->record_latency(latency)

#define RECORD_OPERATION(op) \
    if (g_metrics_collector) g_metrics_collector->record_operation(op)

#define UPDATE_KEY_COUNT(count) \
    if (g_metrics_collector) g_metrics_collector->update_key_count(count)

} // namespace monitoring
} // namespace kvdb