#include "src/stream/change_stream.h"
#include "src/stream/realtime_sync.h"
#include "src/stream/event_driven.h"
#include "src/stream/stream_computing.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace kvdb::stream;

// 测试处理器
class TestStreamProcessor : public StreamProcessor {
public:
    explicit TestStreamProcessor(const std::string& name) : name_(name) {}
    
    void process(const ChangeEvent& event) override {
        std::cout << "[" << name_ << "] Processing event: " 
                  << "Type=" << static_cast<int>(event.type) 
                  << ", Key=" << event.key 
                  << ", Value=" << event.new_value << std::endl;
        processed_count_++;
    }
    
    std::string get_name() const override { return name_; }
    
    size_t get_processed_count() const { return processed_count_; }

private:
    std::string name_;
    std::atomic<size_t> processed_count_{0};
};

// 测试同步目标
class TestSyncTarget : public SyncTarget {
public:
    explicit TestSyncTarget(const std::string& name) : name_(name) {}
    
    void sync_insert(const std::string& key, const std::string& value) override {
        std::cout << "[" << name_ << "] Sync INSERT: " << key << " = " << value << std::endl;
        operations_count_++;
    }
    
    void sync_update(const std::string& key, const std::string& old_value, const std::string& new_value) override {
        std::cout << "[" << name_ << "] Sync UPDATE: " << key << " " << old_value << " -> " << new_value << std::endl;
        operations_count_++;
    }
    
    void sync_delete(const std::string& key, const std::string& value) override {
        std::cout << "[" << name_ << "] Sync DELETE: " << key << " = " << value << std::endl;
        operations_count_++;
    }
    
    std::string get_target_name() const override { return name_; }
    bool is_healthy() const override { return true; }
    
    size_t get_operations_count() const { return operations_count_; }

private:
    std::string name_;
    std::atomic<size_t> operations_count_{0};
};

void test_change_stream() {
    std::cout << "\n=== Testing Change Stream ===" << std::endl;
    
    // 创建流配置
    StreamConfig config;
    config.name = "test_stream";
    config.event_types = {EventType::INSERT, EventType::UPDATE, EventType::DELETE};
    config.buffer_size = 100;
    config.batch_timeout = std::chrono::milliseconds(100);
    
    // 创建流
    auto stream = std::make_shared<ChangeStream>(config);
    
    // 添加处理器
    auto processor = std::make_shared<TestStreamProcessor>("TestProcessor");
    stream->add_processor(processor);
    
    // 启动流
    stream->start();
    
    // 发布测试事件
    for (int i = 0; i < 10; ++i) {
        ChangeEvent event(EventType::INSERT, "key" + std::to_string(i), "", "value" + std::to_string(i));
        stream->publish_event(event);
    }
    
    // 等待处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 获取统计信息
    auto stats = stream->get_stats();
    std::cout << "Stream Stats: Processed=" << stats.events_processed 
              << ", Filtered=" << stats.events_filtered 
              << ", Failed=" << stats.events_failed << std::endl;
    
    std::cout << "Processor processed: " << processor->get_processed_count() << " events" << std::endl;
    
    stream->stop();
}

void test_realtime_sync() {
    std::cout << "\n=== Testing Realtime Sync ===" << std::endl;
    
    // 创建同步配置
    SyncConfig config;
    config.name = "test_sync";
    config.source_patterns = {"user_.*", "order_.*"};
    config.enable_batch_sync = true;
    config.batch_size = 5;
    
    // 创建同步处理器
    auto sync_processor = std::make_shared<RealtimeSyncProcessor>(config);
    
    // 添加同步目标
    auto target1 = std::make_shared<TestSyncTarget>("Target1");
    auto target2 = std::make_shared<TestSyncTarget>("Target2");
    sync_processor->add_target(target1);
    sync_processor->add_target(target2);
    
    // 测试同步事件
    std::vector<ChangeEvent> test_events = {
        ChangeEvent(EventType::INSERT, "user_001", "", "John Doe"),
        ChangeEvent(EventType::UPDATE, "user_001", "John Doe", "John Smith"),
        ChangeEvent(EventType::INSERT, "order_001", "", "Order Data"),
        ChangeEvent(EventType::DELETE, "user_002", "Jane Doe", ""),
        ChangeEvent(EventType::INSERT, "product_001", "", "Product Data") // 不匹配模式
    };
    
    for (const auto& event : test_events) {
        sync_processor->process(event);
    }
    
    // 等待同步完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 获取统计信息
    auto stats = sync_processor->get_stats();
    std::cout << "Sync Stats: Synced=" << stats.synced_operations 
              << ", Failed=" << stats.failed_operations << std::endl;
    
    std::cout << "Target1 operations: " << target1->get_operations_count() << std::endl;
    std::cout << "Target2 operations: " << target2->get_operations_count() << std::endl;
}

void test_event_driven() {
    std::cout << "\n=== Testing Event Driven Architecture ===" << std::endl;
    
    // 启动事件总线
    auto& bus = EventBus::instance();
    bus.start();
    
    // 注册事件处理器
    auto handler1 = std::make_shared<FunctionEventHandler>("Handler1", 
        [](const ChangeEvent& event) {
            std::cout << "Handler1 processing: " << event.key << std::endl;
        }, 10);
    
    auto handler2 = std::make_shared<ConditionalEventHandler>("Handler2",
        [](const ChangeEvent& event) { return event.key.find("important") != std::string::npos; },
        [](const ChangeEvent& event) {
            std::cout << "Handler2 processing important event: " << event.key << std::endl;
        }, 20);
    
    bus.register_handler(handler1);
    bus.register_handler(handler2);
    
    // 添加路由规则
    EventRoute route1;
    route1.name = "user_events";
    route1.event_types = {EventType::INSERT, EventType::UPDATE};
    route1.key_patterns = {"user_.*"};
    route1.handler_names = {"Handler1"};
    route1.priority = 10;
    
    EventRoute route2;
    route2.name = "important_events";
    route2.key_patterns = {".*important.*"};
    route2.handler_names = {"Handler1", "Handler2"};
    route2.priority = 20;
    
    bus.add_route(route1);
    bus.add_route(route2);
    
    // 发布测试事件
    std::vector<ChangeEvent> test_events = {
        ChangeEvent(EventType::INSERT, "user_001", "", "User Data"),
        ChangeEvent(EventType::UPDATE, "user_important_002", "Old", "New"),
        ChangeEvent(EventType::DELETE, "product_001", "Product", ""),
        ChangeEvent(EventType::INSERT, "important_config", "", "Config Data")
    };
    
    for (const auto& event : test_events) {
        bus.publish_async(event);
    }
    
    // 等待处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 获取统计信息
    auto stats = bus.get_stats();
    std::cout << "EventBus Stats: Published=" << stats.events_published 
              << ", Processed=" << stats.events_processed 
              << ", Failed=" << stats.events_failed << std::endl;
    
    bus.stop();
}

void test_stream_computing() {
    std::cout << "\n=== Testing Stream Computing ===" << std::endl;
    
    // 启动流式计算引擎
    auto& engine = StreamComputingEngine::instance();
    engine.start();
    
    // 创建流式计算管道
    auto pipeline = std::make_shared<StreamPipeline>("test_pipeline");
    
    // 添加操作符
    pipeline->filter([](const ChangeEvent& event) {
        return event.key.find("user") != std::string::npos;
    });
    pipeline->map([](const ChangeEvent& event) {
        ChangeEvent mapped = event;
        mapped.new_value = "processed_" + event.new_value;
        return mapped;
    });
    pipeline->group_by([](const ChangeEvent& event) {
        return event.key.substr(0, 4); // 按前4个字符分组
    });
    
    // 添加输出处理器
    auto output_processor = std::make_shared<TestStreamProcessor>("OutputProcessor");
    pipeline->to(output_processor);
    
    // 注册管道
    engine.register_pipeline(pipeline);
    
    // 创建测试数据
    std::vector<ChangeEvent> test_events = {
        ChangeEvent(EventType::INSERT, "user_001", "", "Alice"),
        ChangeEvent(EventType::INSERT, "user_002", "", "Bob"),
        ChangeEvent(EventType::INSERT, "product_001", "", "Product A"),
        ChangeEvent(EventType::UPDATE, "user_001", "Alice", "Alice Smith"),
        ChangeEvent(EventType::INSERT, "user_003", "", "Charlie")
    };
    
    // 处理事件流
    engine.process_stream("test_pipeline", test_events);
    
    // 等待处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 获取统计信息
    auto stats = engine.get_stats();
    std::cout << "Engine Stats: Processed=" << stats.events_processed 
              << ", Failed=" << stats.events_failed << std::endl;
    
    std::cout << "Output processor handled: " << output_processor->get_processed_count() << " events" << std::endl;
    
    engine.stop();
}

void test_window_operations() {
    std::cout << "\n=== Testing Window Operations ===" << std::endl;
    
    // 创建窗口配置
    WindowConfig tumbling_config;
    tumbling_config.type = WindowType::TUMBLING;
    tumbling_config.size = std::chrono::milliseconds(1000);
    
    WindowConfig sliding_config;
    sliding_config.type = WindowType::SLIDING;
    sliding_config.size = std::chrono::milliseconds(2000);
    sliding_config.slide = std::chrono::milliseconds(500);
    
    // 创建窗口操作符
    auto tumbling_op = std::make_shared<WindowOperator>(tumbling_config);
    auto sliding_op = std::make_shared<WindowOperator>(sliding_config);
    
    // 创建测试事件（时间间隔500ms）
    std::vector<ChangeEvent> events;
    auto base_time = std::chrono::system_clock::now();
    
    for (int i = 0; i < 10; ++i) {
        ChangeEvent event(EventType::INSERT, "key" + std::to_string(i), "", "value" + std::to_string(i));
        event.timestamp = base_time + std::chrono::milliseconds(i * 500);
        events.push_back(event);
    }
    
    // 测试滚动窗口
    std::cout << "Testing Tumbling Windows:" << std::endl;
    auto tumbling_result = tumbling_op->process(events);
    std::cout << "Tumbling windows created: " << tumbling_result.size() << std::endl;
    
    // 测试滑动窗口
    std::cout << "Testing Sliding Windows:" << std::endl;
    auto sliding_result = sliding_op->process(events);
    std::cout << "Sliding windows created: " << sliding_result.size() << std::endl;
}

int main() {
    std::cout << "=== KVDB Stream Processing Test ===" << std::endl;
    
    try {
        test_change_stream();
        test_realtime_sync();
        test_event_driven();
        test_stream_computing();
        test_window_operations();
        
        std::cout << "\n=== All Stream Processing Tests Completed Successfully ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}