#include "metrics_server.h"
#include <sstream>
#include <fstream>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

namespace kvdb {
namespace monitoring {

// 全局监控仪表板实例
std::unique_ptr<MonitoringDashboard> g_monitoring_dashboard;

// MetricsServer 实现
MetricsServer::MetricsServer(const std::string& host, int port) 
    : host_(host), port_(port) {
}

MetricsServer::~MetricsServer() {
    stop();
}

void MetricsServer::start() {
    if (running_.load()) return;
    
    running_.store(true);
    server_thread_ = std::thread(&MetricsServer::server_loop, this);
}

void MetricsServer::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void MetricsServer::server_loop() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind socket to port " << port_ << "\n";
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Failed to listen on socket\n";
        close(server_fd);
        return;
    }
    
    std::cout << "Metrics server started on " << host_ << ":" << port_ << "\n";
    
    while (running_.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_.load()) {
                std::cerr << "Failed to accept connection\n";
            }
            continue;
        }
        
        // 读取HTTP请求
        char buffer[4096] = {0};
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        
        if (bytes_read > 0) {
            std::string request(buffer);
            std::string response;
            
            if (request.find("GET /metrics") != std::string::npos) {
                response = handle_metrics_request();
            } else if (request.find("GET /alerts") != std::string::npos) {
                response = handle_alerts_request();
            } else if (request.find("GET /health") != std::string::npos) {
                response = handle_health_request();
            } else {
                // 默认返回仪表板
                if (g_monitoring_dashboard) {
                    std::string dashboard_html = g_monitoring_dashboard->generate_dashboard_html();
                    response = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html\r\n"
                              "Content-Length: " + std::to_string(dashboard_html.length()) + "\r\n"
                              "\r\n" + dashboard_html;
                } else {
                    response = "HTTP/1.1 404 Not Found\r\n\r\n";
                }
            }
            
        (void)write(client_fd, response.c_str(), response.length());
        }
        
        close(client_fd);
    }
    
    close(server_fd);
}

std::string MetricsServer::handle_metrics_request() {
    if (!g_metrics_collector) {
        return "HTTP/1.1 503 Service Unavailable\r\n\r\n";
    }
    
    std::string metrics = g_metrics_collector->export_prometheus();
    
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: " + std::to_string(metrics.length()) + "\r\n"
           "\r\n" + metrics;
}

std::string MetricsServer::handle_alerts_request() {
    if (!g_alert_manager) {
        return "HTTP/1.1 503 Service Unavailable\r\n\r\n";
    }
    
    auto alerts = g_alert_manager->get_alert_history(50);
    
    std::ostringstream json;
    json << "{\n  \"alerts\": [\n";
    
    for (size_t i = 0; i < alerts.size(); ++i) {
        const auto& alert = alerts[i];
        
        json << "    {\n";
        json << "      \"level\": \"";
        switch (alert.level) {
            case AlertLevel::INFO: json << "info"; break;
            case AlertLevel::WARNING: json << "warning"; break;
            case AlertLevel::ERROR: json << "error"; break;
            case AlertLevel::CRITICAL: json << "critical"; break;
        }
        json << "\",\n";
        json << "      \"metric\": \"" << alert.metric_name << "\",\n";
        json << "      \"message\": \"" << alert.message << "\",\n";
        json << "      \"threshold\": " << alert.threshold << ",\n";
        json << "      \"current_value\": " << alert.current_value << ",\n";
        json << "      \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
                   alert.timestamp.time_since_epoch()).count() << "\n";
        json << "    }";
        
        if (i < alerts.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    
    json << "  ]\n}\n";
    
    std::string response_body = json.str();
    
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + std::to_string(response_body.length()) + "\r\n"
           "\r\n" + response_body;
}

std::string MetricsServer::handle_health_request() {
    std::string health = "{\"status\": \"healthy\", \"timestamp\": " + 
                        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()) + "}";
    
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + std::to_string(health.length()) + "\r\n"
           "\r\n" + health;
}

std::string MetricsServer::get_endpoint() const {
    return "http://" + host_ + ":" + std::to_string(port_);
}

// PrometheusExporter 实现
PrometheusExporter::PrometheusExporter(const std::string& output_file,
                                     std::chrono::seconds interval)
    : output_file_(output_file), export_interval_(interval) {
}

PrometheusExporter::~PrometheusExporter() {
    stop();
}

void PrometheusExporter::start() {
    if (running_.load()) return;
    
    running_.store(true);
    export_thread_ = std::thread(&PrometheusExporter::export_loop, this);
}

void PrometheusExporter::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    if (export_thread_.joinable()) {
        export_thread_.join();
    }
}

void PrometheusExporter::export_loop() {
    while (running_.load()) {
        export_metrics();
        std::this_thread::sleep_for(export_interval_);
    }
}

void PrometheusExporter::export_metrics() {
    if (!g_metrics_collector) return;
    
    std::string metrics = g_metrics_collector->export_prometheus();
    
    std::ofstream file(output_file_);
    if (file.is_open()) {
        file << metrics;
    }
}

// JsonExporter 实现
JsonExporter::JsonExporter(const std::string& output_file,
                          std::chrono::seconds interval)
    : output_file_(output_file), export_interval_(interval) {
}

JsonExporter::~JsonExporter() {
    stop();
}

void JsonExporter::start() {
    if (running_.load()) return;
    
    running_.store(true);
    export_thread_ = std::thread(&JsonExporter::export_loop, this);
}

void JsonExporter::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    if (export_thread_.joinable()) {
        export_thread_.join();
    }
}

void JsonExporter::export_loop() {
    while (running_.load()) {
        export_metrics();
        std::this_thread::sleep_for(export_interval_);
    }
}

void JsonExporter::export_metrics() {
    if (!g_metrics_collector) return;
    
    std::string metrics = g_metrics_collector->export_json();
    
    std::ofstream file(output_file_);
    if (file.is_open()) {
        file << metrics;
    }
}

// MonitoringDashboard 实现
MonitoringDashboard::MonitoringDashboard() {
}

MonitoringDashboard::~MonitoringDashboard() {
    stop();
}

void MonitoringDashboard::start(const std::string& host, int port) {
    server_ = std::make_unique<MetricsServer>(host, port);
    server_->start();
    
    // 启动所有导出器
    for (auto& exporter : exporters_) {
        if (auto prometheus = dynamic_cast<PrometheusExporter*>(exporter.get())) {
            prometheus->start();
        } else if (auto json = dynamic_cast<JsonExporter*>(exporter.get())) {
            json->start();
        }
    }
}

void MonitoringDashboard::stop() {
    if (server_) {
        server_->stop();
    }
    
    // 停止所有导出器
    for (auto& exporter : exporters_) {
        if (auto prometheus = dynamic_cast<PrometheusExporter*>(exporter.get())) {
            prometheus->stop();
        } else if (auto json = dynamic_cast<JsonExporter*>(exporter.get())) {
            json->stop();
        }
    }
}

void MonitoringDashboard::add_exporter(std::unique_ptr<MetricsExporter> exporter) {
    exporters_.push_back(std::move(exporter));
}

bool MonitoringDashboard::is_running() const {
    return server_ && server_->is_running();
}

std::string MonitoringDashboard::get_dashboard_url() const {
    if (server_) {
        return server_->get_endpoint();
    }
    return "";
}

std::string MonitoringDashboard::generate_dashboard_html() const {
    std::ostringstream html;
    
    html << R"(<!DOCTYPE html>
<html>
<head>
    <title>KVDB Monitoring Dashboard</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }
        .container { max-width: 1200px; margin: 0 auto; }
        .header { background: #2c3e50; color: white; padding: 20px; border-radius: 5px; margin-bottom: 20px; }
        .metrics-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }
        .metric-card { background: white; padding: 20px; border-radius: 5px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .metric-title { font-size: 18px; font-weight: bold; margin-bottom: 10px; color: #2c3e50; }
        .metric-value { font-size: 24px; font-weight: bold; color: #3498db; }
        .metric-unit { font-size: 14px; color: #7f8c8d; }
        .alert-critical { border-left: 4px solid #e74c3c; }
        .alert-warning { border-left: 4px solid #f39c12; }
        .alert-info { border-left: 4px solid #3498db; }
        .refresh-btn { background: #3498db; color: white; border: none; padding: 10px 20px; border-radius: 3px; cursor: pointer; }
        .refresh-btn:hover { background: #2980b9; }
    </style>
    <script>
        function refreshMetrics() {
            location.reload();
        }
        
        function autoRefresh() {
            setTimeout(function() {
                refreshMetrics();
            }, 30000);
        }
        
        window.onload = autoRefresh;
    </script>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>KVDB Monitoring Dashboard</h1>
            <button class="refresh-btn" onclick="refreshMetrics()">Refresh</button>
        </div>
        
        <div class="metrics-grid">)";
    
    if (g_metrics_collector) {
        auto perf = g_metrics_collector->get_performance_metrics();
        auto resource = g_metrics_collector->get_resource_metrics();
        auto business = g_metrics_collector->get_business_metrics();
        
        // 性能指标
        html << R"(
            <div class="metric-card">
                <div class="metric-title">QPS (Queries Per Second)</div>
                <div class="metric-value">)" << perf.qps.load() << R"(<span class="metric-unit"> req/s</span></div>
            </div>
            
            <div class="metric-card">
                <div class="metric-title">Average Latency</div>
                <div class="metric-value">)" << std::fixed << std::setprecision(2) << perf.avg_latency.load() << R"(<span class="metric-unit"> ms</span></div>
            </div>
            
            <div class="metric-card">
                <div class="metric-title">P99 Latency</div>
                <div class="metric-value">)" << std::fixed << std::setprecision(2) << perf.p99_latency.load() << R"(<span class="metric-unit"> ms</span></div>
            </div>
            
            <div class="metric-card">
                <div class="metric-title">Total Requests</div>
                <div class="metric-value">)" << perf.total_requests.load() << R"(</div>
            </div>
            
            <div class="metric-card">
                <div class="metric-title">CPU Usage</div>
                <div class="metric-value">)" << std::fixed << std::setprecision(1) << resource.cpu_usage.load() << R"(<span class="metric-unit"> %</span></div>
            </div>
            
            <div class="metric-card">
                <div class="metric-title">Memory Usage</div>
                <div class="metric-value">)" << (resource.memory_usage.load() / 1024 / 1024) << R"(<span class="metric-unit"> MB</span></div>
            </div>
            
            <div class="metric-card">
                <div class="metric-title">Total Keys</div>
                <div class="metric-value">)" << business.total_keys.load() << R"(</div>
            </div>
            
            <div class="metric-card">
                <div class="metric-title">Active Connections</div>
                <div class="metric-value">)" << business.active_connections.load() << R"(</div>
            </div>)";
    }
    
    html << R"(
        </div>
        
        <div style="margin-top: 20px;">
            <h2>API Endpoints</h2>
            <ul>
                <li><a href="/metrics">/metrics</a> - Prometheus format metrics</li>
                <li><a href="/alerts">/alerts</a> - Recent alerts in JSON format</li>
                <li><a href="/health">/health</a> - Health check endpoint</li>
            </ul>
        </div>
    </div>
</body>
</html>)";
    
    return html.str();
}

} // namespace monitoring
} // namespace kvdb