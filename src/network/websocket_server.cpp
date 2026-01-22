#include "network/websocket_server.h"
#include <iostream>
#include <chrono>
#include <regex>

WebSocketHandler::WebSocketHandler(KVDB& db) : db_(db) {
    // 设置日志级别
    server_.set_access_channels(websocketpp::log::alevel::all);
    server_.clear_access_channels(websocketpp::log::alevel::frame_payload);
    
    // 初始化 ASIO
    server_.init_asio();
    
    // 设置事件处理器
    server_.set_open_handler([this](connection_hdl hdl) {
        on_open(hdl);
    });
    
    server_.set_close_handler([this](connection_hdl hdl) {
        on_close(hdl);
    });
    
    server_.set_message_handler([this](connection_hdl hdl, message_ptr msg) {
        on_message(hdl, msg);
    });
}

WebSocketHandler::~WebSocketHandler() {
    stop();
}

void WebSocketHandler::start(uint16_t port) {
    try {
        server_.listen(port);
        server_.start_accept();
        
        running_ = true;
        server_thread_ = std::thread(&WebSocketHandler::run_server, this);
        
        std::cout << "[WebSocket] Server listening on port " << port << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] Failed to start server: " << e.what() << std::endl;
    }
}

void WebSocketHandler::stop() {
    if (running_) {
        running_ = false;
        server_.stop();
        
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        
        std::cout << "[WebSocket] Server stopped" << std::endl;
    }
}

void WebSocketHandler::run_server() {
    try {
        server_.run();
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] Server error: " << e.what() << std::endl;
    }
}

void WebSocketHandler::on_open(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.insert(hdl);
    std::cout << "[WebSocket] Client connected" << std::endl;
}

void WebSocketHandler::on_close(connection_hdl hdl) {
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.erase(hdl);
    }
    
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscriptions_.erase(hdl);
    }
    
    std::cout << "[WebSocket] Client disconnected" << std::endl;
}

void WebSocketHandler::on_message(connection_hdl hdl, message_ptr msg) {
    try {
        Json::Value request;
        Json::Reader reader;
        
        if (!reader.parse(msg->get_payload(), request)) {
            send_message(hdl, create_error_response("Invalid JSON"));
            return;
        }
        
        Json::Value response = handle_request(request);
        send_message(hdl, response);
        
    } catch (const std::exception& e) {
        send_message(hdl, create_error_response(e.what()));
    }
}

Json::Value WebSocketHandler::handle_request(const Json::Value& request) {
    if (!request.isMember("method") || !request["method"].isString()) {
        return create_error_response("Missing or invalid method");
    }
    
    std::string method = request["method"].asString();
    Json::Value params = request.get("params", Json::Value());
    
    if (method == "put") {
        return handle_put(params);
    } else if (method == "get") {
        return handle_get(params);
    } else if (method == "delete") {
        return handle_delete(params);
    } else if (method == "batch_put") {
        return handle_batch_put(params);
    } else if (method == "batch_get") {
        return handle_batch_get(params);
    } else if (method == "scan") {
        return handle_scan(params);
    } else if (method == "prefix_scan") {
        return handle_prefix_scan(params);
    } else if (method == "create_snapshot") {
        return handle_create_snapshot(params);
    } else if (method == "release_snapshot") {
        return handle_release_snapshot(params);
    } else if (method == "get_at_snapshot") {
        return handle_get_at_snapshot(params);
    } else if (method == "flush") {
        return handle_flush(params);
    } else if (method == "compact") {
        return handle_compact(params);
    } else if (method == "get_stats") {
        return handle_get_stats(params);
    } else if (method == "set_compaction_strategy") {
        return handle_set_compaction_strategy(params);
    } else {
        return create_error_response("Unknown method: " + method);
    }
}

Json::Value WebSocketHandler::handle_put(const Json::Value& params) {
    if (!params.isMember("key") || !params.isMember("value")) {
        return create_error_response("Missing key or value");
    }
    
    std::string key = params["key"].asString();
    std::string value = params["value"].asString();
    
    bool success = db_.put(key, value);
    if (success) {
        notify_subscribers(key, value, "PUT");
    }
    
    Json::Value result;
    result["success"] = success;
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_get(const Json::Value& params) {
    if (!params.isMember("key")) {
        return create_error_response("Missing key");
    }
    
    std::string key = params["key"].asString();
    std::string value;
    bool found = db_.get(key, value);
    
    Json::Value result;
    result["found"] = found;
    if (found) {
        result["value"] = value;
    }
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_delete(const Json::Value& params) {
    if (!params.isMember("key")) {
        return create_error_response("Missing key");
    }
    
    std::string key = params["key"].asString();
    bool success = db_.del(key);
    
    if (success) {
        notify_subscribers(key, "", "DELETE");
    }
    
    Json::Value result;
    result["success"] = success;
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_batch_put(const Json::Value& params) {
    if (!params.isMember("pairs") || !params["pairs"].isArray()) {
        return create_error_response("Missing or invalid pairs array");
    }
    
    int processed = 0;
    for (const auto& pair : params["pairs"]) {
        if (pair.isMember("key") && pair.isMember("value")) {
            std::string key = pair["key"].asString();
            std::string value = pair["value"].asString();
            
            if (db_.put(key, value)) {
                processed++;
                notify_subscribers(key, value, "PUT");
            }
        }
    }
    
    Json::Value result;
    result["success"] = (processed == params["pairs"].size());
    result["processed_count"] = processed;
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_batch_get(const Json::Value& params) {
    if (!params.isMember("keys") || !params["keys"].isArray()) {
        return create_error_response("Missing or invalid keys array");
    }
    
    Json::Value pairs(Json::arrayValue);
    for (const auto& key_val : params["keys"]) {
        std::string key = key_val.asString();
        std::string value;
        
        if (db_.get(key, value)) {
            Json::Value pair;
            pair["key"] = key;
            pair["value"] = value;
            pairs.append(pair);
        }
    }
    
    Json::Value result;
    result["pairs"] = pairs;
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_scan(const Json::Value& params) {
    if (!params.isMember("start_key") || !params.isMember("end_key")) {
        return create_error_response("Missing start_key or end_key");
    }
    
    std::string start_key = params["start_key"].asString();
    std::string end_key = params["end_key"].asString();
    int limit = params.get("limit", 1000).asInt();
    
    Snapshot snap = db_.get_snapshot();
    auto iter = db_.new_iterator(snap);
    
    Json::Value pairs(Json::arrayValue);
    int count = 0;
    
    for (iter->seek(start_key); 
         iter->valid() && iter->key() <= end_key && count < limit; 
         iter->next()) {
        
        std::string value = iter->value();
        if (!value.empty()) {
            Json::Value pair;
            pair["key"] = iter->key();
            pair["value"] = value;
            pairs.append(pair);
            count++;
        }
    }
    
    Json::Value result;
    result["pairs"] = pairs;
    result["count"] = count;
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_prefix_scan(const Json::Value& params) {
    if (!params.isMember("prefix")) {
        return create_error_response("Missing prefix");
    }
    
    std::string prefix = params["prefix"].asString();
    int limit = params.get("limit", 1000).asInt();
    
    Snapshot snap = db_.get_snapshot();
    auto iter = db_.new_prefix_iterator(snap, prefix);
    
    Json::Value pairs(Json::arrayValue);
    int count = 0;
    
    while (iter->valid() && count < limit) {
        std::string key = iter->key();
        std::string value = iter->value();
        
        // 检查前缀匹配
        if (key.size() < prefix.size() || 
            key.substr(0, prefix.size()) != prefix) {
            break;
        }
        
        if (!value.empty()) {
            Json::Value pair;
            pair["key"] = key;
            pair["value"] = value;
            pairs.append(pair);
            count++;
        }
        iter->next();
    }
    
    Json::Value result;
    result["pairs"] = pairs;
    result["count"] = count;
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_create_snapshot(const Json::Value& params) {
    Snapshot snap = db_.get_snapshot();
    
    Json::Value result;
    result["snapshot_id"] = static_cast<Json::UInt64>(snap.seq);
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_release_snapshot(const Json::Value& params) {
    if (!params.isMember("snapshot_id")) {
        return create_error_response("Missing snapshot_id");
    }
    
    uint64_t snapshot_id = params["snapshot_id"].asUInt64();
    Snapshot snap(snapshot_id);
    db_.release_snapshot(snap);
    
    Json::Value result;
    result["success"] = true;
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_get_at_snapshot(const Json::Value& params) {
    if (!params.isMember("key") || !params.isMember("snapshot_id")) {
        return create_error_response("Missing key or snapshot_id");
    }
    
    std::string key = params["key"].asString();
    uint64_t snapshot_id = params["snapshot_id"].asUInt64();
    
    Snapshot snap(snapshot_id);
    std::string value;
    bool found = db_.get(key, snap, value);
    
    Json::Value result;
    result["found"] = found;
    if (found) {
        result["value"] = value;
    }
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_flush(const Json::Value& params) {
    db_.flush();
    
    Json::Value result;
    result["success"] = true;
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_compact(const Json::Value& params) {
    db_.compact();
    
    Json::Value result;
    result["success"] = true;
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_get_stats(const Json::Value& params) {
    Json::Value result;
    result["memtable_size"] = static_cast<Json::UInt64>(db_.get_memtable_size());
    result["wal_size"] = static_cast<Json::UInt64>(db_.get_wal_size());
    result["cache_hit_rate"] = db_.get_cache_hit_rate();
    
    const auto& stats = db_.get_compaction_stats();
    Json::Value compaction_stats;
    compaction_stats["total_compactions"] = static_cast<Json::UInt64>(stats.total_compactions);
    compaction_stats["bytes_read"] = static_cast<Json::UInt64>(stats.bytes_read);
    compaction_stats["bytes_written"] = static_cast<Json::UInt64>(stats.bytes_written);
    compaction_stats["write_amplification"] = stats.write_amplification;
    compaction_stats["total_time_ms"] = static_cast<Json::UInt64>(stats.total_time.count());
    
    result["compaction_stats"] = compaction_stats;
    return create_success_response(result);
}

Json::Value WebSocketHandler::handle_set_compaction_strategy(const Json::Value& params) {
    if (!params.isMember("strategy")) {
        return create_error_response("Missing strategy");
    }
    
    std::string strategy_str = params["strategy"].asString();
    CompactionStrategyType strategy;
    
    if (strategy_str == "LEVELED") {
        strategy = CompactionStrategyType::LEVELED;
    } else if (strategy_str == "TIERED") {
        strategy = CompactionStrategyType::TIERED;
    } else if (strategy_str == "SIZE_TIERED") {
        strategy = CompactionStrategyType::SIZE_TIERED;
    } else if (strategy_str == "TIME_WINDOW") {
        strategy = CompactionStrategyType::TIME_WINDOW;
    } else {
        return create_error_response("Invalid strategy: " + strategy_str);
    }
    
    db_.set_compaction_strategy(strategy);
    
    Json::Value result;
    result["success"] = true;
    return create_success_response(result);
}

void WebSocketHandler::notify_subscribers(const std::string& key, const std::string& value, const std::string& operation) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    for (auto& [hdl, subs] : subscriptions_) {
        for (const auto& sub : subs) {
            if (matches_pattern(key, sub.pattern) && 
                (sub.include_deletes || operation != "DELETE")) {
                
                Json::Value notification;
                notification["type"] = "notification";
                notification["key"] = key;
                notification["value"] = value;
                notification["operation"] = operation;
                notification["timestamp"] = static_cast<Json::UInt64>(now);
                
                try {
                    send_message(hdl, notification);
                } catch (...) {
                    // 连接已断开，将在下次清理时移除
                }
            }
        }
    }
}

bool WebSocketHandler::matches_pattern(const std::string& key, const std::string& pattern) {
    if (pattern == "*") return true;
    if (pattern.empty()) return key.empty();
    
    // 简单的通配符匹配
    try {
        std::string regex_pattern = pattern;
        std::replace(regex_pattern.begin(), regex_pattern.end(), '*', '.');
        regex_pattern = "^" + regex_pattern + "$";
        std::regex re(regex_pattern);
        return std::regex_match(key, re);
    } catch (...) {
        // 如果正则表达式无效，使用前缀匹配
        return key.find(pattern) == 0;
    }
}

Json::Value WebSocketHandler::create_error_response(const std::string& error_message) {
    Json::Value response;
    response["success"] = false;
    response["error"] = error_message;
    return response;
}

Json::Value WebSocketHandler::create_success_response(const Json::Value& data) {
    Json::Value response;
    response["success"] = true;
    if (!data.isNull()) {
        response["data"] = data;
    }
    return response;
}

void WebSocketHandler::send_message(connection_hdl hdl, const Json::Value& message) {
    Json::StreamWriterBuilder builder;
    std::string json_str = Json::writeString(builder, message);
    
    try {
        server_.send(hdl, json_str, websocketpp::frame::opcode::text);
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] Failed to send message: " << e.what() << std::endl;
    }
}