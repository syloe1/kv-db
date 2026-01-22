#include "network/tcp_server.h"
#include <iostream>
#include <cstring>
#include <system_error>

namespace kvdb {
namespace network {

TCPServer::TCPServer(KVDB& db, uint16_t port) 
    : db_(db), port_(port) {}

TCPServer::~TCPServer() {
    stop();
}

void TCPServer::start() {
    if (running_) return;
    
    // 创建socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::system_error(errno, std::system_category(), "socket creation failed");
    }
    
    // 设置socket选项
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_fd_);
        throw std::system_error(errno, std::system_category(), "setsockopt failed");
    }
    
    // 绑定地址
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);
    
    if (bind(server_fd_, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd_);
        throw std::system_error(errno, std::system_category(), "bind failed");
    }
    
    // 监听
    if (listen(server_fd_, 128) < 0) {
        close(server_fd_);
        throw std::system_error(errno, std::system_category(), "listen failed");
    }
    
    // 启动服务器线程
    running_ = true;
    server_thread_ = std::thread(&TCPServer::run, this);
    
    std::cout << "TCP Server started on port " << port_ << std::endl;
}

void TCPServer::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // 关闭服务器socket来中断accept
    if (server_fd_ != -1) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
    
    // 等待服务器线程结束
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    // 等待所有客户端线程结束
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& thread : client_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        client_threads_.clear();
    }
    
    std::cout << "TCP Server stopped" << std::endl;
}

void TCPServer::run() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            if (running_) {
                std::cerr << "accept failed: " << strerror(errno) << std::endl;
            }
            break;
        }
        
        // 获取客户端IP
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Client connected: " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;
        
        // 创建新线程处理客户端
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_threads_.emplace_back(&TCPServer::handle_client, this, client_fd);
    }
}

void TCPServer::handle_client(int client_fd) {
    while (running_) {
        RequestHeader header;
        
        // 读取请求头
        ssize_t bytes_read = recv(client_fd, &header, sizeof(header), MSG_WAITALL);
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                std::cout << "Client disconnected" << std::endl;
            } else {
                std::cerr << "recv header failed: " << strerror(errno) << std::endl;
            }
            break;
        }
        
        // 验证magic number
        if (header.magic != PROTOCOL_MAGIC) {
            send_response(client_fd, Status::INVALID_REQUEST, "Invalid magic number");
            break;
        }
        
        // 验证数据长度
        if (header.key_length > MAX_KEY_SIZE || header.value_length > MAX_VALUE_SIZE) {
            send_response(client_fd, Status::INVALID_REQUEST, "Data too large");
            break;
        }
        
        // 读取键和值
        std::string key(header.key_length, '\0');
        std::string value(header.value_length, '\0');
        
        if (header.key_length > 0) {
            bytes_read = recv(client_fd, key.data(), header.key_length, MSG_WAITALL);
            if (bytes_read != static_cast<ssize_t>(header.key_length)) {
                send_response(client_fd, Status::INVALID_REQUEST, "Failed to read key");
                break;
            }
        }
        
        if (header.value_length > 0) {
            bytes_read = recv(client_fd, value.data(), header.value_length, MSG_WAITALL);
            if (bytes_read != static_cast<ssize_t>(header.value_length)) {
                send_response(client_fd, Status::INVALID_REQUEST, "Failed to read value");
                break;
            }
        }
        
        // 处理请求
        if (!process_request(client_fd, header, key, value)) {
            break;
        }
    }
    
    close(client_fd);
}

bool TCPServer::process_request(int client_fd, const RequestHeader& header, 
                               const std::string& key, const std::string& value) {
    try {
        switch (static_cast<Opcode>(header.opcode)) {
            case Opcode::PUT:
                db_.put(key, value);
                return send_response(client_fd, Status::SUCCESS);
                
            case Opcode::GET: {
                std::string result;
                if (db_.get(key, result)) {
                    return send_response(client_fd, Status::SUCCESS, result);
                } else {
                    return send_response(client_fd, Status::KEY_NOT_FOUND);
                }
            }
                
            case Opcode::DEL:
                db_.del(key);
                return send_response(client_fd, Status::SUCCESS);
                
            default:
                return send_response(client_fd, Status::INVALID_REQUEST, "Unknown opcode");
        }
    } catch (const std::exception& e) {
        std::cerr << "Processing request failed: " << e.what() << std::endl;
        return send_response(client_fd, Status::INTERNAL_ERROR, e.what());
    }
}

bool TCPServer::send_response(int client_fd, Status status, const std::string& value) {
    ResponseHeader header;
    header.status = static_cast<uint8_t>(status);
    header.value_length = value.size();
    
    // 发送响应头
    if (send(client_fd, &header, sizeof(header), 0) != sizeof(header)) {
        std::cerr << "Failed to send response header" << std::endl;
        return false;
    }
    
    // 发送响应值（如果有）
    if (!value.empty()) {
        if (send(client_fd, value.data(), value.size(), 0) != static_cast<ssize_t>(value.size())) {
            std::cerr << "Failed to send response value" << std::endl;
            return false;
        }
    }
    
    return true;
}

} // namespace network
} // namespace kvdb