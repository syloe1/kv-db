#include "metrics_collector.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

namespace kvdb {
namespace monitoring {

// 全局指标收集器实例
std::unique_ptr<MetricsCollector> g_metrics_collector;

// LatencyStats 实现
LatencyStats::LatencyStats(size_t max_samples) : max_samples_(max_samples) {
    samples_.reserve(max_samples_);
}

void LatencyStats::record(double latency_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (samples_.size() >= max_samples_) {
        // 移除最旧的样本
        samples_.erase(samples_.begin());
    }
    
    samples_.push_back(latency_ms);
}

double LatencyStats::get_average() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    
    if (samples_.empty()) return 0.0;
    
    double sum = 0.0;
    for (double sample : samples_) {
        sum += sample;
    }
    
    return sum / samples_.size();
}

double LatencyStats::get_percentile(double p) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    
    if (samples_.empty()) return 0.0;
    
    std::vector<double> sorted_samples = samples_;
    std::sort(sorted_samples.begin(), sorted_samples.end());
    
    size_t index = static_cast<size_t>((p / 100.0) * (sorted_samples.size() - 1));
    return sorted_samples[index];
}

void LatencyStats::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
}

// MetricsCollector 实现
MetricsCollector::MetricsCollector() 
    : latency_stats_(std::make_unique<LatencyStats>()) {
}

MetricsCollector::~MetricsCollector() {
    stop();
}

void MetricsCollector::start() {
    if (running_.load()) return;
    
    running_.store(true);
    monitor_thread_ = std::thread(&MetricsCollector::monitor_loop, this);
}

void MetricsCollector::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    cv_.notify_all();
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void MetricsCollector::monitor_loop() {
    auto last_time = std::chrono::steady_clock::now();
    uint64_t last_requests = 0;
    uint64_t last_throughput = 0;
    
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait_for(lock, std::chrono::seconds(1), [this] { return !running_.load(); });
        
        if (!running_.load()) break;
        
        auto current_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - last_time).count();
        
        if (duration >= 1000) { // 每秒更新一次
            // 计算QPS
            uint64_t current_requests = total_requests_.load();
            uint64_t requests_diff = current_requests - last_requests;
            qps_.store(requests_diff);
            last_requests = current_requests;
            
            // 计算吞吐量
            uint64_t current_throughput = throughput_.load();
            throughput_.store(current_throughput - last_throughput);
            last_throughput = current_throughput;
            
            // 更新延迟统计
            avg_latency_.store(latency_stats_->get_average());
            p99_latency_.store(latency_stats_->get_percentile(99.0));
            
            // 更新资源指标
            cpu_usage_.store(get_cpu_usage());
            memory_usage_.store(get_memory_usage());
            disk_usage_.store(get_disk_usage());
            
            // 检查告警阈值
            check_thresholds();
            
            last_time = current_time;
        }
    }
}

void MetricsCollector::check_thresholds() {
    // CPU使用率检查
    double cpu = cpu_usage_.load();
    if (cpu >= thresholds_.cpu_critical) {
        add_alert(AlertLevel::CRITICAL, "cpu_usage", 
                 "CPU usage is critically high", thresholds_.cpu_critical, cpu);
    } else if (cpu >= thresholds_.cpu_warning) {
        add_alert(AlertLevel::WARNING, "cpu_usage", 
                 "CPU usage is high", thresholds_.cpu_warning, cpu);
    }
    
    // 内存使用率检查
    double memory_percent = (memory_usage_.load() / 1024.0 / 1024.0 / 1024.0) * 100;
    if (memory_percent >= thresholds_.memory_critical) {
        add_alert(AlertLevel::CRITICAL, "memory_usage", 
                 "Memory usage is critically high", thresholds_.memory_critical, memory_percent);
    } else if (memory_percent >= thresholds_.memory_warning) {
        add_alert(AlertLevel::WARNING, "memory_usage", 
                 "Memory usage is high", thresholds_.memory_warning, memory_percent);
    }
    
    // 延迟检查
    double latency = avg_latency_.load();
    if (latency >= thresholds_.latency_critical) {
        add_alert(AlertLevel::CRITICAL, "latency", 
                 "Average latency is critically high", thresholds_.latency_critical, latency);
    } else if (latency >= thresholds_.latency_warning) {
        add_alert(AlertLevel::WARNING, "latency", 
                 "Average latency is high", thresholds_.latency_warning, latency);
    }
    
    // QPS检查
    uint64_t qps = qps_.load();
    if (qps >= thresholds_.qps_critical) {
        add_alert(AlertLevel::CRITICAL, "qps", 
                 "QPS is critically high", thresholds_.qps_critical, qps);
    } else if (qps >= thresholds_.qps_warning) {
        add_alert(AlertLevel::WARNING, "qps", 
                 "QPS is high", thresholds_.qps_warning, qps);
    }
}

void MetricsCollector::add_alert(AlertLevel level, const std::string& metric, 
                                const std::string& message, double threshold, double value) {
    std::lock_guard<std::mutex> lock(alerts_mutex_);
    
    Alert alert;
    alert.level = level;
    alert.metric_name = metric;
    alert.message = message;
    alert.threshold = threshold;
    alert.current_value = value;
    alert.timestamp = std::chrono::system_clock::now();
    
    alerts_.push_back(alert);
    
    // 限制告警数量
    if (alerts_.size() > 1000) {
        alerts_.erase(alerts_.begin());
    }
}

double MetricsCollector::get_cpu_usage() {
#ifdef __linux__
    static unsigned long long last_total = 0, last_idle = 0;
    
    std::ifstream file("/proc/stat");
    std::string line;
    std::getline(file, line);
    
    std::istringstream iss(line);
    std::string cpu;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    
    iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    
    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    unsigned long long total_diff = total - last_total;
    unsigned long long idle_diff = idle - last_idle;
    
    double cpu_usage = 0.0;
    if (total_diff > 0) {
        cpu_usage = 100.0 * (total_diff - idle_diff) / total_diff;
    }
    
    last_total = total;
    last_idle = idle;
    
    return cpu_usage;
#else
    return 0.0; // 其他平台暂不支持
#endif
}

uint64_t MetricsCollector::get_memory_usage() {
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (info.totalram - info.freeram) * info.mem_unit;
    }
#endif
    return 0;
}

uint64_t MetricsCollector::get_disk_usage() {
    struct statvfs stat;
    if (statvfs(".", &stat) == 0) {
        return (stat.f_blocks - stat.f_bavail) * stat.f_frsize;
    }
    return 0;
}

void MetricsCollector::record_request() {
    total_requests_.fetch_add(1);
}

void MetricsCollector::record_latency(double latency_ms) {
    latency_stats_->record(latency_ms);
}

void MetricsCollector::record_operation(const std::string& op_type) {
    if (op_type == "get") {
        get_count_.fetch_add(1);
    } else if (op_type == "put") {
        put_count_.fetch_add(1);
    } else if (op_type == "delete") {
        delete_count_.fetch_add(1);
    } else if (op_type == "scan") {
        scan_count_.fetch_add(1);
    }
}

void MetricsCollector::record_throughput(uint64_t bytes) {
    throughput_.fetch_add(bytes);
}

void MetricsCollector::update_key_count(uint64_t count) {
    total_keys_.store(count);
}

void MetricsCollector::update_data_size(uint64_t size) {
    total_data_size_.store(size);
}

void MetricsCollector::update_connections(int delta) {
    if (delta > 0) {
        active_connections_.fetch_add(delta);
    } else {
        active_connections_.fetch_sub(-delta);
    }
}

void MetricsCollector::record_transaction(bool committed) {
    transaction_count_.fetch_add(1);
    if (committed) {
        commit_count_.fetch_add(1);
    } else {
        abort_count_.fetch_add(1);
    }
}

PerformanceMetrics MetricsCollector::get_performance_metrics() const {
    PerformanceMetrics metrics;
    metrics.qps = qps_.load();
    metrics.total_requests = total_requests_.load();
    metrics.avg_latency = avg_latency_.load();
    metrics.p99_latency = p99_latency_.load();
    metrics.throughput = throughput_.load();
    metrics.get_count = get_count_.load();
    metrics.put_count = put_count_.load();
    metrics.delete_count = delete_count_.load();
    metrics.scan_count = scan_count_.load();
    return metrics;
}

ResourceMetrics MetricsCollector::get_resource_metrics() const {
    ResourceMetrics metrics;
    metrics.cpu_usage = cpu_usage_.load();
    metrics.memory_usage = memory_usage_.load();
    metrics.disk_usage = disk_usage_.load();
    metrics.network_in = network_in_.load();
    metrics.network_out = network_out_.load();
    metrics.memtable_size = memtable_size_.load();
    metrics.sstable_count = sstable_count_.load();
    metrics.wal_size = wal_size_.load();
    return metrics;
}

BusinessMetrics MetricsCollector::get_business_metrics() const {
    BusinessMetrics metrics;
    metrics.total_keys = total_keys_.load();
    metrics.total_data_size = total_data_size_.load();
    metrics.growth_rate = growth_rate_.load();
    metrics.active_connections = active_connections_.load();
    metrics.transaction_count = transaction_count_.load();
    metrics.commit_count = commit_count_.load();
    metrics.abort_count = abort_count_.load();
    return metrics;
}

std::vector<Alert> MetricsCollector::get_alerts() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(alerts_mutex_));
    return alerts_;
}

void MetricsCollector::clear_alerts() {
    std::lock_guard<std::mutex> lock(alerts_mutex_);
    alerts_.clear();
}

void MetricsCollector::set_thresholds(const Thresholds& thresholds) {
    thresholds_ = thresholds;
}

std::string MetricsCollector::export_prometheus() const {
    std::ostringstream oss;
    
    // 性能指标
    oss << "# HELP kvdb_qps Queries per second\n";
    oss << "# TYPE kvdb_qps gauge\n";
    oss << "kvdb_qps " << qps_.load() << "\n";
    
    oss << "# HELP kvdb_latency_avg Average latency in milliseconds\n";
    oss << "# TYPE kvdb_latency_avg gauge\n";
    oss << "kvdb_latency_avg " << avg_latency_.load() << "\n";
    
    oss << "# HELP kvdb_latency_p99 P99 latency in milliseconds\n";
    oss << "# TYPE kvdb_latency_p99 gauge\n";
    oss << "kvdb_latency_p99 " << p99_latency_.load() << "\n";
    
    // 资源指标
    oss << "# HELP kvdb_cpu_usage CPU usage percentage\n";
    oss << "# TYPE kvdb_cpu_usage gauge\n";
    oss << "kvdb_cpu_usage " << cpu_usage_.load() << "\n";
    
    oss << "# HELP kvdb_memory_usage Memory usage in bytes\n";
    oss << "# TYPE kvdb_memory_usage gauge\n";
    oss << "kvdb_memory_usage " << memory_usage_.load() << "\n";
    
    // 业务指标
    oss << "# HELP kvdb_total_keys Total number of keys\n";
    oss << "# TYPE kvdb_total_keys gauge\n";
    oss << "kvdb_total_keys " << total_keys_.load() << "\n";
    
    return oss.str();
}

std::string MetricsCollector::export_json() const {
    std::ostringstream oss;
    
    oss << "{\n";
    oss << "  \"performance\": {\n";
    oss << "    \"qps\": " << qps_.load() << ",\n";
    oss << "    \"avg_latency\": " << avg_latency_.load() << ",\n";
    oss << "    \"p99_latency\": " << p99_latency_.load() << ",\n";
    oss << "    \"total_requests\": " << total_requests_.load() << "\n";
    oss << "  },\n";
    oss << "  \"resources\": {\n";
    oss << "    \"cpu_usage\": " << cpu_usage_.load() << ",\n";
    oss << "    \"memory_usage\": " << memory_usage_.load() << ",\n";
    oss << "    \"disk_usage\": " << disk_usage_.load() << "\n";
    oss << "  },\n";
    oss << "  \"business\": {\n";
    oss << "    \"total_keys\": " << total_keys_.load() << ",\n";
    oss << "    \"total_data_size\": " << total_data_size_.load() << ",\n";
    oss << "    \"active_connections\": " << active_connections_.load() << "\n";
    oss << "  }\n";
    oss << "}\n";
    
    return oss.str();
}

} // namespace monitoring
} // namespace kvdb