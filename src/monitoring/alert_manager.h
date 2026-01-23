#pragma once

#include "metrics_collector.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace kvdb {
namespace monitoring {

// 告警处理器接口
class AlertHandler {
public:
    virtual ~AlertHandler() = default;
    virtual void handle_alert(const Alert& alert) = 0;
    virtual std::string get_name() const = 0;
};

// 日志告警处理器
class LogAlertHandler : public AlertHandler {
private:
    std::string log_file_;
    
public:
    explicit LogAlertHandler(const std::string& log_file = "alerts.log");
    void handle_alert(const Alert& alert) override;
    std::string get_name() const override { return "LogAlertHandler"; }
};

// 邮件告警处理器
class EmailAlertHandler : public AlertHandler {
private:
    std::string smtp_server_;
    std::string from_email_;
    std::vector<std::string> to_emails_;
    
public:
    EmailAlertHandler(const std::string& smtp_server, 
                     const std::string& from_email,
                     const std::vector<std::string>& to_emails);
    void handle_alert(const Alert& alert) override;
    std::string get_name() const override { return "EmailAlertHandler"; }
};

// HTTP Webhook告警处理器
class WebhookAlertHandler : public AlertHandler {
private:
    std::string webhook_url_;
    std::string auth_token_;
    
public:
    WebhookAlertHandler(const std::string& webhook_url, 
                       const std::string& auth_token = "");
    void handle_alert(const Alert& alert) override;
    std::string get_name() const override { return "WebhookAlertHandler"; }
};

// 告警规则
struct AlertRule {
    std::string name;
    std::string metric_name;
    std::function<bool(double)> condition;
    AlertLevel level;
    std::string message_template;
    std::chrono::seconds cooldown{300}; // 5分钟冷却期
    std::chrono::system_clock::time_point last_triggered;
    bool enabled = true;
};

// 告警管理器
class AlertManager {
private:
    std::vector<std::unique_ptr<AlertHandler>> handlers_;
    std::vector<AlertRule> rules_;
    std::mutex rules_mutex_;
    
    // 监控线程
    std::thread alert_thread_;
    std::atomic<bool> running_{false};
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    
    // 告警历史
    std::vector<Alert> alert_history_;
    std::mutex history_mutex_;
    size_t max_history_size_ = 10000;
    
    void alert_loop();
    void check_rules();
    bool should_trigger_alert(const AlertRule& rule) const;
    std::string format_message(const AlertRule& rule, double value) const;
    
public:
    AlertManager();
    ~AlertManager();
    
    // 启动/停止告警管理
    void start();
    void stop();
    
    // 添加告警处理器
    void add_handler(std::unique_ptr<AlertHandler> handler);
    
    // 添加告警规则
    void add_rule(const AlertRule& rule);
    void remove_rule(const std::string& name);
    void enable_rule(const std::string& name, bool enabled);
    
    // 预定义规则
    void add_default_rules();
    
    // 手动触发告警
    void trigger_alert(const Alert& alert);
    
    // 获取告警历史
    std::vector<Alert> get_alert_history(size_t limit = 100) const;
    void clear_alert_history();
    
    // 获取规则状态
    std::vector<AlertRule> get_rules() const;
    
    // 配置
    void set_max_history_size(size_t size);
};

// 全局告警管理器实例
extern std::unique_ptr<AlertManager> g_alert_manager;

} // namespace monitoring
} // namespace kvdb