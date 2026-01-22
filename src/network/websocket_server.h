#pragma once
#include "db/kv_db.h"
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <jsoncpp/json/json.h>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

typedef websocketpp::server<websocketpp::config::asio> WebSocketServer;
typedef WebSocketServer::message_ptr message_ptr;
typedef websocketpp::connection_hdl connection_hdl;

class WebSocketHandler {
public:
    explicit WebSocketHandler(KVDB& db);
    ~WebSocketHandler();

    void start(uint16_t port = 8080);
    void stop();

private:
    KVDB& db_;
    WebSocketServer server_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    
    // 连接管理
    std::mutex connections_mutex_;
    std::unordered_set<connection_hdl, std::owner_less<connection_hdl>> connections_;
    
    // 订阅管理
    struct Subscription {
        std::string pattern;
        bool include_deletes;
        connection_hdl connection;
    };
    
    std::mutex subscriptions_mutex_;
    std::unordered_map<connection_hdl, std::vector<Subscription>, 
                       std::owner_less<connection_hdl>> subscriptions_;

    // WebSocket 事件处理
    void on_open(connection_hdl hdl);
    void on_close(connection_hdl hdl);
    void on_message(connection_hdl hdl, message_ptr msg);

    // 消息处理
    Json::Value handle_request(const Json::Value& request);
    Json::Value handle_put(const Json::Value& params);
    Json::Value handle_get(const Json::Value& params);
    Json::Value handle_delete(const Json::Value& params);
    Json::Value handle_batch_put(const Json::Value& params);
    Json::Value handle_batch_get(const Json::Value& params);
    Json::Value handle_scan(const Json::Value& params);
    Json::Value handle_prefix_scan(const Json::Value& params);
    Json::Value handle_create_snapshot(const Json::Value& params);
    Json::Value handle_release_snapshot(const Json::Value& params);
    Json::Value handle_get_at_snapshot(const Json::Value& params);
    Json::Value handle_flush(const Json::Value& params);
    Json::Value handle_compact(const Json::Value& params);
    Json::Value handle_get_stats(const Json::Value& params);
    Json::Value handle_set_compaction_strategy(const Json::Value& params);
    Json::Value handle_subscribe(connection_hdl hdl, const Json::Value& params);
    Json::Value handle_unsubscribe(connection_hdl hdl, const Json::Value& params);

    // 订阅通知
    void notify_subscribers(const std::string& key, const std::string& value, const std::string& operation);
    bool matches_pattern(const std::string& key, const std::string& pattern);

    // 辅助方法
    Json::Value create_error_response(const std::string& error_message);
    Json::Value create_success_response(const Json::Value& data = Json::Value());
    void send_message(connection_hdl hdl, const Json::Value& message);
    void run_server();
};