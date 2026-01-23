#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <unordered_map>

namespace kvdb {
namespace ops {

// 数据增长模式
enum class GrowthPattern {
    LINEAR,
    EXPONENTIAL,
    LOGARITHMIC,
    SEASONAL,
    CUSTOM
};

// 容量指标
struct CapacityMetrics {
    // 存储指标
    size_t total_storage_bytes = 0;
    size_t used_storage_bytes = 0;
    size_t available_storage_bytes = 0;
    double storage_utilization = 0.0;
    
    // 内存指标
    size_t total_memory_bytes = 0;
    size_t used_memory_bytes = 0;
    size_t available_memory_bytes = 0;
    double memory_utilization = 0.0;
    
    // 性能指标
    double current_qps = 0.0;
    double max_qps = 0.0;
    double avg_latency_ms = 0.0;
    
    // 连接指标
    size_t active_connections = 0;
    size_t max_connections = 0;
    
    std::chrono::system_clock::time_point timestamp;
};

// 增长预测结果
struct GrowthPrediction {
    GrowthPattern pattern;
    double confidence_score; // 0.0 - 1.0
    
    // 预测数据
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> predictions;
    
    // 统计信息
    double growth_rate; // 增长率
    double seasonal_factor; // 季节性因子
    std::string description;
};

// 容量预警
struct CapacityAlert {
    enum class AlertType {
        STORAGE_HIGH,
        MEMORY_HIGH,
        QPS_HIGH,
        CONNECTION_HIGH,
        GROWTH_RATE_HIGH
    };
    
    AlertType type;
    std::string message;
    double current_value;
    double threshold_value;
    std::chrono::system_clock::time_point predicted_time;
    std::string recommendation;
};

// 扩容建议
struct ScalingRecommendation {
    enum class ScalingType {
        VERTICAL,   // 垂直扩容（增加资源）
        HORIZONTAL, // 水平扩容（增加节点）
        HYBRID      // 混合扩容
    };
    
    ScalingType type;
    std::string description;
    
    // 资源建议
    size_t recommended_memory_gb = 0;
    size_t recommended_storage_gb = 0;
    size_t recommended_cpu_cores = 0;
    size_t recommended_nodes = 0;
    
    // 成本估算
    double estimated_cost_per_month = 0.0;
    
    // 时间建议
    std::chrono::system_clock::time_point recommended_time;
    
    // 优先级
    int priority = 0; // 1-10, 10最高
};

// 容量规划配置
struct PlannerConfig {
    // 预测参数
    std::chrono::hours prediction_window{24 * 30}; // 30天
    size_t min_data_points = 100;
    double confidence_threshold = 0.7;
    
    // 阈值设置
    double storage_warning_threshold = 0.8;  // 80%
    double storage_critical_threshold = 0.9; // 90%
    double memory_warning_threshold = 0.8;
    double memory_critical_threshold = 0.9;
    double qps_warning_threshold = 0.8;
    double qps_critical_threshold = 0.9;
    
    // 增长率阈值
    double high_growth_rate_threshold = 0.1; // 10%每天
    
    // 采样间隔
    std::chrono::minutes sampling_interval{5};
};

class CapacityPlanner {
public:
    explicit CapacityPlanner(const PlannerConfig& config = {});
    ~CapacityPlanner();

    // 记录容量指标
    void record_metrics(const CapacityMetrics& metrics);
    
    // 获取当前指标
    CapacityMetrics get_current_metrics();
    
    // 获取历史指标
    std::vector<CapacityMetrics> get_historical_metrics(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end);
    
    // 数据增长预测
    GrowthPrediction predict_storage_growth();
    GrowthPrediction predict_memory_growth();
    GrowthPrediction predict_qps_growth();
    
    // 自定义指标预测
    GrowthPrediction predict_metric_growth(
        const std::string& metric_name,
        const std::vector<std::pair<std::chrono::system_clock::time_point, double>>& data);
    
    // 容量预警
    std::vector<CapacityAlert> get_capacity_alerts();
    
    // 扩容建议
    std::vector<ScalingRecommendation> get_scaling_recommendations();
    
    // 容量规划报告
    std::string generate_capacity_report();
    
    // 模拟扩容效果
    CapacityMetrics simulate_scaling(const ScalingRecommendation& recommendation);
    
    // 成本分析
    double calculate_scaling_cost(const ScalingRecommendation& recommendation);
    
    // 配置更新
    void update_config(const PlannerConfig& config);
    
    // 数据导出
    bool export_capacity_data(const std::string& output_path);
    
    // 清理历史数据
    void cleanup_old_data(std::chrono::hours retention_period = std::chrono::hours(24 * 90));

private:
    PlannerConfig config_;
    std::vector<CapacityMetrics> metrics_history_;
    std::mutex metrics_mutex_;
    
    // 预测算法
    GrowthPrediction linear_regression_predict(
        const std::vector<std::pair<std::chrono::system_clock::time_point, double>>& data);
    GrowthPrediction exponential_predict(
        const std::vector<std::pair<std::chrono::system_clock::time_point, double>>& data);
    GrowthPrediction seasonal_predict(
        const std::vector<std::pair<std::chrono::system_clock::time_point, double>>& data);
    
    // 数据处理
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> 
    extract_metric_series(const std::string& metric_name);
    
    void smooth_data(std::vector<std::pair<std::chrono::system_clock::time_point, double>>& data);
    
    // 异常检测
    std::vector<size_t> detect_anomalies(
        const std::vector<std::pair<std::chrono::system_clock::time_point, double>>& data);
    
    // 模式识别
    GrowthPattern identify_growth_pattern(
        const std::vector<std::pair<std::chrono::system_clock::time_point, double>>& data);
    
    // 预警生成
    std::vector<CapacityAlert> generate_storage_alerts();
    std::vector<CapacityAlert> generate_memory_alerts();
    std::vector<CapacityAlert> generate_performance_alerts();
    
    // 建议生成
    ScalingRecommendation generate_storage_recommendation();
    ScalingRecommendation generate_memory_recommendation();
    ScalingRecommendation generate_performance_recommendation();
    
    // 工具方法
    double calculate_growth_rate(
        const std::vector<std::pair<std::chrono::system_clock::time_point, double>>& data);
    double calculate_confidence_score(const GrowthPrediction& prediction);
    std::chrono::system_clock::time_point predict_threshold_breach(
        const GrowthPrediction& prediction, double threshold);
};

// 自动容量监控器
class AutoCapacityMonitor {
public:
    AutoCapacityMonitor(CapacityPlanner* planner, 
                       std::chrono::minutes interval = std::chrono::minutes(5));
    ~AutoCapacityMonitor();
    
    void start();
    void stop();
    
    // 设置回调函数
    void set_alert_callback(std::function<void(const CapacityAlert&)> callback);
    void set_recommendation_callback(std::function<void(const ScalingRecommendation&)> callback);

private:
    CapacityPlanner* planner_;
    std::chrono::minutes interval_;
    std::atomic<bool> running_;
    std::thread monitor_thread_;
    
    std::function<void(const CapacityAlert&)> alert_callback_;
    std::function<void(const ScalingRecommendation&)> recommendation_callback_;
    
    void monitor_loop();
    CapacityMetrics collect_system_metrics();
};

} // namespace ops
} // namespace kvdb