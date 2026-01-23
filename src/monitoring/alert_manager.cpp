#include "alert_manager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

namespace kvdb {
namespace monitoring {

// 全局告警管理器实例
std::unique_ptr<AlertManager> g_alert_manager;

// LogAlertHandler 实现
LogAlertHandler::LogAlertHandler(const std::string& log_file) 
    : log_file_(log_file) {
}

void LogAlertHandler::handle_alert(const Alert& alert) {
    std::ofstream file(log_file_, std::ios::app);
    if (!file.is_open()) return;
    
    auto time_t = std::chrono::system_clock::to_time_t(alert.timestamp);
    auto tm = *std::localtime(&time_t);
    
    std::string level_str;
    switch (alert.level) {
        case AlertLevel::INFO: level_str = "INFO"; break;
        case AlertLevel::WARNING: level_str = "WARNING"; break;
        case AlertLevel::ERROR: level_str = "ERROR"; break;
        case AlertLevel::CRITICAL: level_str = "CRITICAL"; break;
    }
    
    file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") 
         << " [" << level_str << "] "
         << alert.metric_name << ": " << alert.message
         << " (threshold: " << alert.threshold 
         << ", current: " << alert.current_value << ")\n";
}

// EmailAlertHandler 实现
EmailAlertHandler::EmailAlertHandler(const std::string& smtp_server,
                                   const std::string& from_email,
                                   const std::vector<std::string>& to_emails)
    : smtp_server_(smtp_server), from_email_(from_email), to_emails_(to_emails) {
}

void EmailAlertHandler::handle_alert(const Alert& alert) {
    // 简化的邮件发送实现
    // 实际生产环境中需要使用专业的邮件库
    std::ostringstream subject;
    subject << "[KVDB Alert] " << alert.metric_name << " - ";
    
    switch (alert.level) {
        case AlertLevel::INFO: subject << "INFO"; break;
        case AlertLevel::WARNING: subject << "WARNING"; break;
        case AlertLevel::ERROR: subject << "ERROR"; break;
        case AlertLevel::CRITICAL: subject << "CRITICAL"; break;
    }
    
    std::ostringstream body;
    body << "Alert Details:\n"
         << "Metric: " << alert.metric_name << "\n"
         << "Message: " << alert.message << "\n"
         << "Threshold: " << alert.threshold << "\n"
         << "Current Value: " << alert.current_value << "\n"
         << "Timestamp: " << std::chrono::duration_cast<std::chrono::seconds>(
                alert.timestamp.time_since_epoch()).count() << "\n";
    
    // 这里应该调用实际的邮件发送API
    // 为了演示，我们只是记录到日志
    std::ofstream log("email_alerts.log", std::ios::app);
    if (log.is_open()) {
        log << "EMAIL ALERT: " << subject.str() << "\n" << body.str() << "\n\n";
    }
}

// WebhookAlertHandler 实现
WebhookAlertHandler::WebhookAlertHandler(const std::string& webhook_url,
                                       const std::string& auth_token)
    : webhook_url_(webhook_url), auth_token_(auth_token) {
}

void WebhookAlertHandler::handle_alert(const Alert& alert) {
    // 简化的Webhook实现，不依赖curl
    std::ostringstream json;
    json << "{\n"
         << "  \"metric\": \"" << alert.metric_name << "\",\n"
         << "  \"level\": \"";
    
    switch (alert.level) {
        case AlertLevel::INFO: json << "info"; break;
        case AlertLevel::WARNING: json << "warning"; break;
        case AlertLevel::ERROR: json << "error"; break;
        case AlertLevel::CRITICAL: json << "critical"; break;
    }
    
    json << "\",\n"
         << "  \"message\": \"" << alert.message << "\",\n"
         << "  \"threshold\": " << alert.threshold << ",\n"
         << "  \"current_value\": " << alert.current_value << ",\n"
         << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
                alert.timestamp.time_since_epoch()).count() << "\n"
         << "}";
    
    // 记录到文件而不是发送HTTP请求
    std::ofstream log("webhook_alerts.log", std::ios::app);
    if (log.is_open()) {
        log << "Webhook alert (would send to " << webhook_url_ << "): " 
            << json.str() << "\n";
    }
}

// AlertManager 实现
AlertManager::AlertManager() {
}

AlertManager::~AlertManager() {
    stop();
}

void AlertManager::start() {
    if (running_.load()) return;
    
    running_.store(true);
    alert_thread_ = std::thread(&AlertManager::alert_loop, this);
}

void AlertManager::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    cv_.notify_all();
    
    if (alert_thread_.joinable()) {
        alert_thread_.join();
    }
}

void AlertManager::alert_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait_for(lock, std::chrono::seconds(10), [this] { return !running_.load(); });
        
        if (!running_.load()) break;
        
        check_rules();
    }
}

void AlertManager::check_rules() {
    if (!g_metrics_collector) return;
    
    auto perf_metrics = g_metrics_collector->get_performance_metrics();
    auto resource_metrics = g_metrics_collector->get_resource_metrics();
    auto business_metrics = g_metrics_collector->get_business_metrics();
    
    std::lock_guard<std::mutex> lock(rules_mutex_);
    
    for (auto& rule : rules_) {
        if (!rule.enabled) continue;
        
        double value = 0.0;
        
        // 获取对应指标的值
        if (rule.metric_name == "qps") {
            value = perf_metrics.qps;
        } else if (rule.metric_name == "avg_latency") {
            value = perf_metrics.avg_latency;
        } else if (rule.metric_name == "p99_latency") {
            value = perf_metrics.p99_latency;
        } else if (rule.metric_name == "cpu_usage") {
            value = resource_metrics.cpu_usage;
        } else if (rule.metric_name == "memory_usage") {
            value = resource_metrics.memory_usage;
        } else if (rule.metric_name == "disk_usage") {
            value = resource_metrics.disk_usage;
        } else if (rule.metric_name == "active_connections") {
            value = business_metrics.active_connections;
        }
        
        // 检查条件
        if (rule.condition(value) && should_trigger_alert(rule)) {
            Alert alert;
            alert.level = rule.level;
            alert.metric_name = rule.metric_name;
            alert.message = format_message(rule, value);
            alert.threshold = 0.0; // 规则中没有固定阈值
            alert.current_value = value;
            alert.timestamp = std::chrono::system_clock::now();
            
            trigger_alert(alert);
            rule.last_triggered = alert.timestamp;
        }
    }
}

bool AlertManager::should_trigger_alert(const AlertRule& rule) const {
    auto now = std::chrono::system_clock::now();
    auto time_since_last = now - rule.last_triggered;
    return time_since_last >= rule.cooldown;
}

std::string AlertManager::format_message(const AlertRule& rule, double value) const {
    std::string message = rule.message_template;
    
    // 简单的模板替换
    size_t pos = message.find("{value}");
    if (pos != std::string::npos) {
        message.replace(pos, 7, std::to_string(value));
    }
    
    return message;
}

void AlertManager::add_handler(std::unique_ptr<AlertHandler> handler) {
    handlers_.push_back(std::move(handler));
}

void AlertManager::add_rule(const AlertRule& rule) {
    std::lock_guard<std::mutex> lock(rules_mutex_);
    rules_.push_back(rule);
}

void AlertManager::remove_rule(const std::string& name) {
    std::lock_guard<std::mutex> lock(rules_mutex_);
    rules_.erase(std::remove_if(rules_.begin(), rules_.end(),
                               [&name](const AlertRule& rule) {
                                   return rule.name == name;
                               }), rules_.end());
}

void AlertManager::enable_rule(const std::string& name, bool enabled) {
    std::lock_guard<std::mutex> lock(rules_mutex_);
    for (auto& rule : rules_) {
        if (rule.name == name) {
            rule.enabled = enabled;
            break;
        }
    }
}

void AlertManager::add_default_rules() {
    // CPU使用率告警
    AlertRule cpu_warning;
    cpu_warning.name = "cpu_high_warning";
    cpu_warning.metric_name = "cpu_usage";
    cpu_warning.condition = [](double value) { return value > 70.0; };
    cpu_warning.level = AlertLevel::WARNING;
    cpu_warning.message_template = "CPU usage is high: {value}%";
    add_rule(cpu_warning);
    
    AlertRule cpu_critical;
    cpu_critical.name = "cpu_high_critical";
    cpu_critical.metric_name = "cpu_usage";
    cpu_critical.condition = [](double value) { return value > 90.0; };
    cpu_critical.level = AlertLevel::CRITICAL;
    cpu_critical.message_template = "CPU usage is critically high: {value}%";
    add_rule(cpu_critical);
    
    // 延迟告警
    AlertRule latency_warning;
    latency_warning.name = "latency_high_warning";
    latency_warning.metric_name = "avg_latency";
    latency_warning.condition = [](double value) { return value > 100.0; };
    latency_warning.level = AlertLevel::WARNING;
    latency_warning.message_template = "Average latency is high: {value}ms";
    add_rule(latency_warning);
    
    AlertRule latency_critical;
    latency_critical.name = "latency_high_critical";
    latency_critical.metric_name = "avg_latency";
    latency_critical.condition = [](double value) { return value > 500.0; };
    latency_critical.level = AlertLevel::CRITICAL;
    latency_critical.message_template = "Average latency is critically high: {value}ms";
    add_rule(latency_critical);
    
    // QPS告警
    AlertRule qps_high;
    qps_high.name = "qps_high";
    qps_high.metric_name = "qps";
    qps_high.condition = [](double value) { return value > 5000.0; };
    qps_high.level = AlertLevel::WARNING;
    qps_high.message_template = "QPS is very high: {value}";
    add_rule(qps_high);
}

void AlertManager::trigger_alert(const Alert& alert) {
    // 添加到历史记录
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        alert_history_.push_back(alert);
        
        if (alert_history_.size() > max_history_size_) {
            alert_history_.erase(alert_history_.begin());
        }
    }
    
    // 通知所有处理器
    for (auto& handler : handlers_) {
        try {
            handler->handle_alert(alert);
        } catch (const std::exception& e) {
            // 记录处理器错误，但不影响其他处理器
            std::ofstream log("alert_handler_errors.log", std::ios::app);
            if (log.is_open()) {
                log << "Handler " << handler->get_name() 
                    << " failed: " << e.what() << "\n";
            }
        }
    }
}

std::vector<Alert> AlertManager::get_alert_history(size_t limit) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(history_mutex_));
    
    if (limit >= alert_history_.size()) {
        return alert_history_;
    }
    
    return std::vector<Alert>(alert_history_.end() - limit, alert_history_.end());
}

void AlertManager::clear_alert_history() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    alert_history_.clear();
}

std::vector<AlertRule> AlertManager::get_rules() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(rules_mutex_));
    return rules_;
}

void AlertManager::set_max_history_size(size_t size) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    max_history_size_ = size;
    
    if (alert_history_.size() > max_history_size_) {
        alert_history_.erase(alert_history_.begin(), 
                           alert_history_.end() - max_history_size_);
    }
}

} // namespace monitoring
} // namespace kvdb