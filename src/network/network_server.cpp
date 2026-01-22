#include "network/network_server.h"
#include <iostream>

NetworkServer::NetworkServer(KVDB& db) : db_(db) {
}

NetworkServer::~NetworkServer() {
    stop_all();
}

void NetworkServer::start_grpc(const std::string& address) {
    if (!grpc_running_) {
        try {
            grpc_server_ = std::make_unique<GRPCServer>(db_, address);
            grpc_server_->start();
            grpc_running_ = true;
            std::cout << "[NetworkServer] gRPC service started on " << address << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[NetworkServer] Failed to start gRPC service: " << e.what() << std::endl;
        }
    }
}

void NetworkServer::start_websocket(uint16_t port) {
    if (!websocket_running_) {
        try {
            websocket_server_ = std::make_unique<WebSocketHandler>(db_);
            websocket_server_->start(port);
            websocket_running_ = true;
            std::cout << "[NetworkServer] WebSocket service started on port " << port << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[NetworkServer] Failed to start WebSocket service: " << e.what() << std::endl;
        }
    }
}

void NetworkServer::start_all() {
    std::cout << "[NetworkServer] Starting all network services..." << std::endl;
    start_grpc();
    start_websocket();
    std::cout << "[NetworkServer] All services started" << std::endl;
}

void NetworkServer::stop_grpc() {
    if (grpc_running_ && grpc_server_) {
        grpc_server_->stop();
        grpc_running_ = false;
        std::cout << "[NetworkServer] gRPC service stopped" << std::endl;
    }
}

void NetworkServer::stop_websocket() {
    if (websocket_running_ && websocket_server_) {
        websocket_server_->stop();
        websocket_running_ = false;
        std::cout << "[NetworkServer] WebSocket service stopped" << std::endl;
    }
}

void NetworkServer::stop_all() {
    std::cout << "[NetworkServer] Stopping all network services..." << std::endl;
    stop_grpc();
    stop_websocket();
    std::cout << "[NetworkServer] All services stopped" << std::endl;
}

void NetworkServer::wait_for_shutdown() {
    if (grpc_server_) {
        grpc_server_->wait_for_shutdown();
    }
}