#include "src/stream/change_stream.h"
#include "src/stream/realtime_sync.h"
#include "src/stream/event_driven.h"
#include "src/stream/stream_computing.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>

using namespace kvdb::stream;

// 用户活动处理器
class UserActivityProcessor : public StreamProcessor {
public:
    void process(const ChangeEvent& event) override {
        if (event.key.find("user_") == 0) {
            std::cout << "📊 用户活动: " << event.key 
                      << " 操作类型: " << get_event_type_name(event.type)
                      << " 时间: " << format_time(event.timestamp) << std::endl;
            
            // 统计用户活动
            user_activity_count_++;
            
            // 检测异常活动
            if (event.type == EventType::DELETE) {
                std::cout << "⚠️  检测到用户删除操作: " << event.key << std::endl;
            }
        }
    }
    
    std::string get_name() const override { return "UserActivityProcessor"; }
    
    size_t get_activity_count() const { return user_activity_count_; }

private:
    std::atomic<size_t> user_activity_count_{0};
    
    std::string get_event_type_name(EventType type) {
        switch (type) {
            case EventType::INSERT: return "新增";
            case EventType::UPDATE: return "更新";
            case EventType::DELETE: return "删除";
            default: return "未知";
        }
    }
    
    std::string format_time(const std::chrono::system_clock::time_point& tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }
};

// 订单处理器
class OrderProcessor : public StreamProcessor {
public:
    void process(const ChangeEvent& event) override {
        if (event.key.find("order_") == 0) {
            std::cout << "🛒 订单处理: " << event.key 
                      << " 状态: " << get_event_type_name(event.type) << std::endl;
            
            order_count_++;
            
            // 计算订单金额（模拟）
            if (event.type == EventType::INSERT) {
                double amount = generate_random_amount();
                total_amount_.store(total_amount_.load() + amount);
                std::cout << "💰 当前总金额: ¥" << total_amount_.load() << std::endl;
            }
        }
    }
    
    std::string get_name() const override { return "OrderProcessor"; }
    
    size_t get_order_count() const { return order_count_; }
    double get_total_amount() const { return total_amount_; }

private:
    std::atomic<size_t> order_count_{0};
    std::atomic<double> total_amount_{0.0};
    
    std::string get_event_type_name(EventType type) {
        switch (type) {
            case EventType::INSERT: return "创建";
            case EventType::UPDATE: return "更新";
            case EventType::DELETE: return "取消";
            default: return "未知";
        }
    }
    
    double generate_random_amount() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<> dis(10.0, 1000.0);
        return dis(gen);
    }
};

// 实时告警处理器
class AlertHandler : public EventHandler {
public:
    void handle(const ChangeEvent& event) override {
        // 检测高价值订单
        if (event.key.find("order_") == 0 && event.new_value.find("high_value") != std::string::npos) {
            std::cout << "🚨 高价值订单告警: " << event.key << std::endl;
            alert_count_++;
        }
        
        // 检测频繁操作
        if (event.key.find("user_") == 0) {
            auto now = std::chrono::system_clock::now();
            user_operations_[event.key].push_back(now);
            
            // 清理旧记录
            auto& ops = user_operations_[event.key];
            ops.erase(std::remove_if(ops.begin(), ops.end(),
                [now](const std::chrono::system_clock::time_point& tp) {
                    return (now - tp) > std::chrono::minutes(1);
                }), ops.end());
            
            // 检测频繁操作
            if (ops.size() > 10) {
                std::cout << "⚠️  频繁操作告警: " << event.key 
                          << " (1分钟内操作" << ops.size() << "次)" << std::endl;
            }
        }
    }
    
    std::string get_handler_name() const override { return "AlertHandler"; }
    int get_priority() const override { return 100; }
    
    size_t get_alert_count() const { return alert_count_; }

private:
    std::atomic<size_t> alert_count_{0};
    std::unordered_map<std::string, std::vector<std::chrono::system_clock::time_point>> user_operations_;
};

// 数据同步目标（模拟外部系统）
class ExternalSystemSync : public SyncTarget {
public:
    explicit ExternalSystemSync(const std::string& system_name) 
        : system_name_(system_name) {}
    
    void sync_insert(const std::string& key, const std::string& value) override {
        std::cout << "🔄 [" << system_name_ << "] 同步新增: " << key << std::endl;
        sync_count_++;
    }
    
    void sync_update(const std::string& key, const std::string& old_value, const std::string& new_value) override {
        std::cout << "🔄 [" << system_name_ << "] 同步更新: " << key << std::endl;
        sync_count_++;
    }
    
    void sync_delete(const std::string& key, const std::string& value) override {
        std::cout << "🔄 [" << system_name_ << "] 同步删除: " << key << std::endl;
        sync_count_++;
    }
    
    std::string get_target_name() const override { return system_name_; }
    bool is_healthy() const override { return true; }
    
    size_t get_sync_count() const { return sync_count_; }

private:
    std::string system_name_;
    std::atomic<size_t> sync_count_{0};
};

void simulate_user_activities(std::shared_ptr<ChangeStream> stream) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> user_dis(1, 100);
    std::uniform_int_distribution<> action_dis(0, 2);
    
    for (int i = 0; i < 20; ++i) {
        std::string user_key = "user_" + std::to_string(user_dis(gen));
        EventType event_type = static_cast<EventType>(action_dis(gen));
        
        ChangeEvent event(event_type, user_key, "old_data", "new_data");
        stream->publish_event(event);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void simulate_order_activities(std::shared_ptr<ChangeStream> stream) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> order_dis(1000, 9999);
    std::uniform_int_distribution<> value_dis(0, 10);
    
    for (int i = 0; i < 15; ++i) {
        std::string order_key = "order_" + std::to_string(order_dis(gen));
        std::string value = (value_dis(gen) > 7) ? "high_value_order" : "normal_order";
        
        ChangeEvent event(EventType::INSERT, order_key, "", value);
        stream->publish_event(event);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

int main() {
    std::cout << "🚀 KVDB 流式处理演示程序" << std::endl;
    std::cout << "=========================" << std::endl;
    
    try {
        // 1. 创建变更流
        StreamConfig stream_config;
        stream_config.name = "main_stream";
        stream_config.event_types = {EventType::INSERT, EventType::UPDATE, EventType::DELETE};
        stream_config.buffer_size = 200;
        stream_config.batch_timeout = std::chrono::milliseconds(50);
        
        auto main_stream = std::make_shared<ChangeStream>(stream_config);
        
        // 2. 添加流处理器
        auto user_processor = std::make_shared<UserActivityProcessor>();
        auto order_processor = std::make_shared<OrderProcessor>();
        
        main_stream->add_processor(user_processor);
        main_stream->add_processor(order_processor);
        
        // 3. 设置实时同步
        SyncConfig sync_config;
        sync_config.name = "external_sync";
        sync_config.source_patterns = {"user_.*", "order_.*"};
        sync_config.enable_batch_sync = true;
        sync_config.batch_size = 5;
        
        auto sync_processor = std::make_shared<RealtimeSyncProcessor>(sync_config);
        
        // 添加外部系统同步目标
        auto crm_sync = std::make_shared<ExternalSystemSync>("CRM系统");
        auto analytics_sync = std::make_shared<ExternalSystemSync>("分析系统");
        
        sync_processor->add_target(crm_sync);
        sync_processor->add_target(analytics_sync);
        
        main_stream->add_processor(sync_processor);
        
        // 4. 设置事件驱动处理
        auto& event_bus = EventBus::instance();
        event_bus.start();
        
        auto alert_handler = std::make_shared<AlertHandler>();
        event_bus.register_handler(alert_handler);
        
        // 添加事件路由
        EventRoute alert_route;
        alert_route.name = "alert_route";
        alert_route.event_types = {EventType::INSERT, EventType::UPDATE, EventType::DELETE};
        alert_route.handler_names = {"AlertHandler"};
        alert_route.priority = 100;
        
        event_bus.add_route(alert_route);
        
        // 5. 设置流式计算
        auto& computing_engine = StreamComputingEngine::instance();
        computing_engine.start();
        
        auto analytics_pipeline = std::make_shared<StreamPipeline>("analytics_pipeline");
        
        // 添加流式计算操作
        analytics_pipeline->filter([](const ChangeEvent& event) {
            return event.key.find("user_") == 0 || event.key.find("order_") == 0;
        });
        
        analytics_pipeline->map([](const ChangeEvent& event) {
            ChangeEvent processed = event;
            processed.metadata["processed_time"] = std::to_string(
                std::chrono::system_clock::now().time_since_epoch().count());
            return processed;
        });
        
        // 窗口操作
        WindowConfig window_config;
        window_config.type = WindowType::TUMBLING;
        window_config.size = std::chrono::seconds(5);
        analytics_pipeline->window(window_config);
        
        computing_engine.register_pipeline(analytics_pipeline);
        
        // 6. 启动流处理
        main_stream->start();
        
        std::cout << "\n✅ 流式处理系统已启动" << std::endl;
        std::cout << "📡 开始模拟数据流..." << std::endl;
        
        // 7. 模拟数据流
        std::thread user_thread(simulate_user_activities, main_stream);
        std::thread order_thread(simulate_order_activities, main_stream);
        
        // 定期发布事件到事件总线
        std::thread event_publisher([&]() {
            for (int i = 0; i < 30; ++i) {
                ChangeEvent event(EventType::INSERT, "user_" + std::to_string(i % 10), "", "data");
                event_bus.publish_async(event);
                
                // 处理流式计算
                std::vector<ChangeEvent> batch = {event};
                computing_engine.process_stream("analytics_pipeline", batch);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        });
        
        // 等待模拟完成
        user_thread.join();
        order_thread.join();
        event_publisher.join();
        
        // 等待处理完成
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 8. 显示统计信息
        std::cout << "\n📊 处理统计:" << std::endl;
        std::cout << "─────────────────────────" << std::endl;
        
        auto stream_stats = main_stream->get_stats();
        std::cout << "变更流: 处理 " << stream_stats.events_processed 
                  << " 个事件, 过滤 " << stream_stats.events_filtered 
                  << " 个事件" << std::endl;
        
        std::cout << "用户活动: " << user_processor->get_activity_count() << " 次" << std::endl;
        std::cout << "订单处理: " << order_processor->get_order_count() 
                  << " 个订单, 总金额 ¥" << std::fixed << std::setprecision(2) 
                  << order_processor->get_total_amount() << std::endl;
        
        auto sync_stats = sync_processor->get_stats();
        std::cout << "数据同步: " << sync_stats.synced_operations << " 次操作" << std::endl;
        std::cout << "CRM同步: " << crm_sync->get_sync_count() << " 次" << std::endl;
        std::cout << "分析同步: " << analytics_sync->get_sync_count() << " 次" << std::endl;
        
        auto bus_stats = event_bus.get_stats();
        std::cout << "事件总线: 发布 " << bus_stats.events_published 
                  << " 个事件, 处理 " << bus_stats.events_processed << " 个事件" << std::endl;
        std::cout << "告警处理: " << alert_handler->get_alert_count() << " 次告警" << std::endl;
        
        auto engine_stats = computing_engine.get_stats();
        std::cout << "流式计算: 处理 " << engine_stats.events_processed << " 个事件" << std::endl;
        
        // 9. 清理资源
        main_stream->stop();
        event_bus.stop();
        computing_engine.stop();
        
        std::cout << "\n🎉 流式处理演示完成!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 演示失败: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}