#pragma once

#include "data_migration_manager.h"
#include "performance_analyzer.h"
#include "capacity_planner.h"
#include "automation_manager.h"
#include <memory>
#include <string>

namespace kvdb {
namespace ops {

// 运维管理器配置
struct OpsManagerConfig {
    // 数据迁移配置
    MigrationConfig migration_config;
    
    // 性能分析配置
    AnalyzerConfig analyzer_config;
    
    // 容量规划配置
    PlannerConfig planner_config;
    
    // 自动化配置
    bool enable_auto_scaling = true;
    bool enable_auto_backup = true;
    bool enable_auto_cleanup = true;
    bool enable_performance_monitoring = true;
    bool enable_capacity_monitoring = true;
    
    // 报告配置
    std::string report_output_path = "./ops_reports";
    std::chrono::hours report_interval{24}; // 每天生成报告
};

// 运维仪表板数据
struct OpsDashboard {
    // 系统状态
    struct SystemStatus {
        bool healthy = true;
        std::string status_message;
        std::chrono::system_clock::time_point last_check;
    } system_status;
    
    // 性能指标
    PerformanceStats performance_stats;
    
    // 容量指标
    CapacityMetrics capacity_metrics;
    
    // 自动化状态
    struct AutomationStatus {
        size_t active_tasks = 0;
        size_t completed_tasks = 0;
        size_t failed_tasks = 0;
        std::chrono::system_clock::time_point last_execution;
    } automation_status;
    
    // 告警信息
    std::vector<CapacityAlert> active_alerts;
    
    // 最近活动
    std::vector<std::string> recent_activities;
};

// 综合运维管理器
class OpsManager {
public:
    explicit OpsManager(const OpsManagerConfig& config = {});
    ~OpsManager();

    // 初始化和启动
    bool initialize();
    void start();
    void stop();
    bool is_running() const;
    
    // 组件访问
    DataMigrationManager* get_migration_manager() { return migration_manager_.get(); }
    PerformanceAnalyzer* get_performance_analyzer() { return performance_analyzer_.get(); }
    CapacityPlanner* get_capacity_planner() { return capacity_planner_.get(); }
    AutomationManager* get_automation_manager() { return automation_manager_.get(); }
    
    // 数据迁移接口
    std::string export_database(const std::string& db_path,
                               const std::string& output_path,
                               ExportFormat format = ExportFormat::JSON);
    
    std::string import_database(const std::string& input_path,
                               const std::string& db_path,
                               ImportFormat format = ImportFormat::AUTO_DETECT);
    
    std::string migrate_database(const std::string& source_db,
                                const std::string& target_db);
    
    // 性能分析接口
    std::vector<SlowQuery> get_slow_queries(size_t limit = 100);
    PerformanceStats get_performance_stats();
    std::string generate_performance_report();
    
    // 容量规划接口
    std::vector<CapacityAlert> get_capacity_alerts();
    std::vector<ScalingRecommendation> get_scaling_recommendations();
    std::string generate_capacity_report();
    
    // 自动化运维接口
    std::string create_auto_scaling_policy(const ScalingPolicy& policy);
    std::string create_auto_backup_policy(const BackupPolicy& policy);
    std::string create_auto_cleanup_policy(const CleanupPolicy& policy);
    
    // 仪表板接口
    OpsDashboard get_dashboard_data();
    std::string generate_dashboard_html();
    
    // 健康检查
    bool perform_health_check();
    std::vector<std::string> get_health_issues();
    
    // 综合报告
    std::string generate_ops_report();
    bool export_ops_data(const std::string& output_path);
    
    // 配置管理
    void update_config(const OpsManagerConfig& config);
    OpsManagerConfig get_config() const;
    
    // 事件通知
    void set_alert_callback(std::function<void(const std::string&)> callback);
    void set_notification_callback(std::function<void(const std::string&, const std::string&)> callback);

private:
    OpsManagerConfig config_;
    
    // 核心组件
    std::unique_ptr<DataMigrationManager> migration_manager_;
    std::unique_ptr<PerformanceAnalyzer> performance_analyzer_;
    std::unique_ptr<CapacityPlanner> capacity_planner_;
    std::unique_ptr<AutomationManager> automation_manager_;
    
    // 监控组件
    std::unique_ptr<AutoCapacityMonitor> capacity_monitor_;
    
    // 状态管理
    std::atomic<bool> running_;
    std::thread report_thread_;
    std::mutex state_mutex_;
    
    // 回调函数
    std::function<void(const std::string&)> alert_callback_;
    std::function<void(const std::string&, const std::string&)> notification_callback_;
    
    // 内部方法
    void report_generation_loop();
    void setup_default_automation();
    void setup_monitoring();
    void handle_capacity_alert(const CapacityAlert& alert);
    void handle_scaling_recommendation(const ScalingRecommendation& recommendation);
    
    // 报告生成
    std::string generate_migration_report();
    std::string generate_automation_report();
    std::string generate_health_report();
    
    // 工具方法
    std::string format_timestamp(std::chrono::system_clock::time_point time);
    std::string format_duration(std::chrono::microseconds duration);
    std::string format_bytes(size_t bytes);
    std::string format_percentage(double percentage);
};

// 全局运维管理器实例
class GlobalOpsManager {
public:
    static OpsManager& instance();
    static void initialize(const OpsManagerConfig& config = {});
    static void shutdown();

private:
    static std::unique_ptr<OpsManager> instance_;
    static std::mutex instance_mutex_;
};

// 便利宏
#define OPS_MANAGER() GlobalOpsManager::instance()

} // namespace ops
} // namespace kvdb