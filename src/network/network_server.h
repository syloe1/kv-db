#pragma once
#include "db/kv_db.h"
#include "network/grpc_server.h"
#include "network/websocket_server.h"
#include <memory>
#include <thread>
#include <atomic>

class NetworkServer {
public:
    explicit NetworkServer(KVDB& db);
    ~NetworkServer();

    // 启动服务
    void start_grpc(const std::string& address = "0.0.0.0:50051");
    void start_websocket(uint16_t port = 8080);
    void start_all();

    // 停止服务
    void stop_grpc();
    void stop_websocket();
    void stop_all();

    // 等待服务结束
    void wait_for_shutdown();

    // 状态查询
    bool is_grpc_running() const { return grpc_running_; }
    bool is_websocket_running() const { return websocket_running_; }

private:
    KVDB& db_;
    
    // gRPC 服务
    std::unique_ptr<GRPCServer> grpc_server_;
    std::atomic<bool> grpc_running_{false};
    
    // WebSocket 服务
    std::unique_ptr<WebSocketHandler> websocket_server_;
    std::atomic<bool> websocket_running_{false};
};