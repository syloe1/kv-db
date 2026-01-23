#pragma once

#include "metrics_collector.h"
#include "alert_manager.h"
#include <string>
#include <thread>
#include <atomic>
#include <memory>

namespace kvdb {
namespace monitoring {

// HTTP指标服务器
class MetricsServer {
private:
    std::string host_;
    int port_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    
    void server_loop();
    std::string handle_metrics_request();
    std::string handle_alerts_request();
    std::string handle_health_request();
    
public:
    MetricsServer(const std::string& host = "0.0.0.0", int port = 8080);
    ~MetricsServer();
    
    void start();
    void stop();
    
    bool is_running() const { return running_.load(); }
    std::string get_endpoint() const;
};

// 指标导出器接口
class MetricsExporter {
public:
    virtual ~MetricsExporter() = default;
    virtual void export_metrics() = 0;
    virtual std::string get_name() const = 0;
};

// Prometheus导出器
class PrometheusExporter : public MetricsExporter {
private:
    std::string output_file_;
    std::chrono::seconds export_interval_;
    std::thread export_thread_;
    std::atomic<bool> running_{false};
    
    void export_loop();
    
public:
    PrometheusExporter(const std::string& output_file = "metrics.prom",
                      std::chrono::seconds interval = std::chrono::seconds(30));
    ~PrometheusExporter();
    
    void start();
    void stop();
    void export_metrics() override;
    std::string get_name() const override { return "PrometheusExporter"; }
};

// JSON导出器
class JsonExporter : public MetricsExporter {
private:
    std::string output_file_;
    std::chrono::seconds export_interval_;
    std::thread export_thread_;
    std::atomic<bool> running_{false};
    
    void export_loop();
    
public:
    JsonExporter(const std::string& output_file = "metrics.json",
                std::chrono::seconds interval = std::chrono::seconds(60));
    ~JsonExporter();
    
    void start();
    void stop();
    void export_metrics() override;
    std::string get_name() const override { return "JsonExporter"; }
};

// 监控仪表板
class MonitoringDashboard {
private:
    std::unique_ptr<MetricsServer> server_;
    std::vector<std::unique_ptr<MetricsExporter>> exporters_;
    
public:
    MonitoringDashboard();
    ~MonitoringDashboard();
    
    // 启动/停止仪表板
    void start(const std::string& host = "0.0.0.0", int port = 8080);
    void stop();
    
    // 添加导出器
    void add_exporter(std::unique_ptr<MetricsExporter> exporter);
    
    // 获取状态
    bool is_running() const;
    std::string get_dashboard_url() const;
    
    // 生成HTML仪表板
    std::string generate_dashboard_html() const;
};

// 全局监控仪表板实例
extern std::unique_ptr<MonitoringDashboard> g_monitoring_dashboard;

} // namespace monitoring
} // namespace kvdb