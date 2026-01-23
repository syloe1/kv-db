#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

namespace kvdb {
namespace ops {

// 自动化任务类型
enum class AutomationTaskType {
    AUTO_SCALING,
    AUTO_BACKUP,
    AUTO_CLEANUP,
    AUTO_OPTIMIZATION,
    AUTO_MONITORING,
    CUSTOM
};

// 任务状态
enum class TaskStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED,
    SCHEDULED
};

// 触发条件类型
enum class TriggerType {
    TIME_BASED,     // 基于时间
    METRIC_BASED,   // 基于指标
    EVENT_BASED,    // 基于事件
    MANUAL          // 手动触发
};

// 触发条件
struct TriggerCondition {
    TriggerType type;
    
    // 时间触发
    std::string cron_expression; // cron格式
    std::chrono::system_clock::time_point next_run_time;
    
    // 指标触发
    std::string metric_name;
    std::string operator_type; // >, <, >=, <=, ==, !=
    double threshold_value;
    std::chrono::minutes evaluation_window{5};
    
    // 事件触发
    std::string event_type;
    std::unordered_map<std::string, std::string> event_filters;
    
    bool is_satisfied() const;
};

// 自动化任务
struct AutomationTask {
    std::string task_id;
    std::string name;
    std::string description;
    AutomationTaskType type;
    TaskStatus status;
    
    // 触发条件
    std::vector<TriggerCondition> triggers;
    
    // 执行配置
    std::string script_path;
    std::vector<std::string> parameters;
    std::chrono::minutes timeout{30};
    int max_retries = 3;
    int current_retries = 0;
    
    // 执行历史
    std::chrono::system_clock::time_point last_run_time;
    std::chrono::system_clock::time_point next_run_time;
    std::string last_result;
    std::string last_error;
    
    // 统计信息
    size_t total_runs = 0;
    size_t successful_runs = 0;
    size_t failed_runs = 0;
    std::chrono::microseconds avg_execution_time{0};
    
    bool enabled = true;
};

// 扩容策略
struct ScalingPolicy {
    std::string policy_id;
    std::string name;
    
    // 扩容触发条件
    double scale_up_cpu_threshold = 0.8;
    double scale_up_memory_threshold = 0.8;
    double scale_up_qps_threshold = 0.8;
    std::chrono::minutes scale_up_duration{5};
    
    // 缩容触发条件
    double scale_down_cpu_threshold = 0.3;
    double scale_down_memory_threshold = 0.3;
    double scale_down_qps_threshold = 0.3;
    std::chrono::minutes scale_down_duration{15};
    
    // 扩容参数
    int min_instances = 1;
    int max_instances = 10;
    int scale_up_step = 1;
    int scale_down_step = 1;
    std::chrono::minutes cooldown_period{10};
    
    bool enabled = true;
};

// 备份策略
struct BackupPolicy {
    std::string policy_id;
    std::string name;
    
    // 备份类型
    enum class BackupType {
        FULL,
        INCREMENTAL,
        DIFFERENTIAL
    } type = BackupType::FULL;
    
    // 备份调度
    std::string schedule; // cron表达式
    std::string backup_path;
    
    // 保留策略
    int retention_days = 30;
    int max_backups = 10;
    
    // 压缩和加密
    bool enable_compression = true;
    bool enable_encryption = false;
    std::string encryption_key;
    
    bool enabled = true;
};

// 清理策略
struct CleanupPolicy {
    std::string policy_id;
    std::string name;
    
    // 清理目标
    std::vector<std::string> target_paths;
    std::vector<std::string> file_patterns;
    
    // 清理条件
    std::chrono::hours max_age{24 * 7}; // 7天
    size_t max_size_mb = 1000; // 1GB
    
    // 清理调度
    std::string schedule; // cron表达式
    
    bool enabled = true;
};

class AutomationManager {
public:
    AutomationManager();
    ~AutomationManager();

    // 任务管理
    std::string create_task(const AutomationTask& task);
    bool update_task(const std::string& task_id, const AutomationTask& task);
    bool delete_task(const std::string& task_id);
    AutomationTask get_task(const std::string& task_id);
    std::vector<AutomationTask> list_tasks();
    
    // 任务执行
    bool execute_task(const std::string& task_id);
    bool cancel_task(const std::string& task_id);
    bool enable_task(const std::string& task_id);
    bool disable_task(const std::string& task_id);
    
    // 自动扩容管理
    std::string create_scaling_policy(const ScalingPolicy& policy);
    bool update_scaling_policy(const std::string& policy_id, const ScalingPolicy& policy);
    bool delete_scaling_policy(const std::string& policy_id);
    std::vector<ScalingPolicy> list_scaling_policies();
    
    // 自动备份管理
    std::string create_backup_policy(const BackupPolicy& policy);
    bool update_backup_policy(const std::string& policy_id, const BackupPolicy& policy);
    bool delete_backup_policy(const std::string& policy_id);
    std::vector<BackupPolicy> list_backup_policies();
    
    // 自动清理管理
    std::string create_cleanup_policy(const CleanupPolicy& policy);
    bool update_cleanup_policy(const std::string& policy_id, const CleanupPolicy& policy);
    bool delete_cleanup_policy(const std::string& policy_id);
    std::vector<CleanupPolicy> list_cleanup_policies();
    
    // 调度器控制
    void start_scheduler();
    void stop_scheduler();
    bool is_scheduler_running() const;
    
    // 事件处理
    void trigger_event(const std::string& event_type, 
                      const std::unordered_map<std::string, std::string>& event_data);
    
    // 指标更新
    void update_metric(const std::string& metric_name, double value);
    
    // 执行历史
    struct ExecutionHistory {
        std::string task_id;
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        TaskStatus status;
        std::string result;
        std::string error_message;
    };
    
    std::vector<ExecutionHistory> get_execution_history(
        const std::string& task_id = "", 
        size_t limit = 100);
    
    // 统计信息
    struct AutomationStats {
        size_t total_tasks = 0;
        size_t active_tasks = 0;
        size_t successful_executions = 0;
        size_t failed_executions = 0;
        std::chrono::microseconds avg_execution_time{0};
        std::chrono::system_clock::time_point last_updated;
    };
    
    AutomationStats get_automation_stats();
    
    // 配置导入导出
    bool export_configuration(const std::string& output_path);
    bool import_configuration(const std::string& input_path);

private:
    std::unordered_map<std::string, std::unique_ptr<AutomationTask>> tasks_;
    std::unordered_map<std::string, std::unique_ptr<ScalingPolicy>> scaling_policies_;
    std::unordered_map<std::string, std::unique_ptr<BackupPolicy>> backup_policies_;
    std::unordered_map<std::string, std::unique_ptr<CleanupPolicy>> cleanup_policies_;
    
    std::mutex tasks_mutex_;
    std::mutex policies_mutex_;
    
    // 调度器
    std::atomic<bool> scheduler_running_;
    std::thread scheduler_thread_;
    std::condition_variable scheduler_cv_;
    
    // 执行器
    std::vector<std::thread> executor_threads_;
    std::queue<std::string> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 指标存储
    std::unordered_map<std::string, double> current_metrics_;
    std::mutex metrics_mutex_;
    
    // 执行历史
    std::vector<ExecutionHistory> execution_history_;
    std::mutex history_mutex_;
    
    // 内部方法
    void scheduler_loop();
    void executor_loop();
    bool execute_task_internal(AutomationTask* task);
    bool check_trigger_conditions(const AutomationTask& task);
    std::chrono::system_clock::time_point calculate_next_run_time(const TriggerCondition& trigger);
    
    // 具体执行方法
    bool execute_scaling_task(const AutomationTask& task);
    bool execute_backup_task(const AutomationTask& task);
    bool execute_cleanup_task(const AutomationTask& task);
    bool execute_custom_task(const AutomationTask& task);
    
    // 扩容逻辑
    bool should_scale_up(const ScalingPolicy& policy);
    bool should_scale_down(const ScalingPolicy& policy);
    bool perform_scale_up(const ScalingPolicy& policy);
    bool perform_scale_down(const ScalingPolicy& policy);
    
    // 备份逻辑
    bool perform_backup(const BackupPolicy& policy);
    bool cleanup_old_backups(const BackupPolicy& policy);
    
    // 清理逻辑
    bool perform_cleanup(const CleanupPolicy& policy);
    
    // 工具方法
    std::string generate_id();
    bool parse_cron_expression(const std::string& cron, 
                              std::chrono::system_clock::time_point& next_time);
    bool execute_script(const std::string& script_path, 
                       const std::vector<std::string>& parameters,
                       std::string& result,
                       std::string& error);
};

// 便利函数
AutomationTask create_auto_scaling_task(const std::string& name, 
                                       const ScalingPolicy& policy);
AutomationTask create_auto_backup_task(const std::string& name, 
                                      const BackupPolicy& policy);
AutomationTask create_auto_cleanup_task(const std::string& name, 
                                       const CleanupPolicy& policy);

} // namespace ops
} // namespace kvdb