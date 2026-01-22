#include "network/http_server.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <system_error>
#include <algorithm>
#include <cctype>

namespace kvdb {
namespace network {

HTTPServer::HTTPServer(KVDB& db, uint16_t port) 
    : db_(db), port_(port) {}

HTTPServer::~HTTPServer() {
    stop();
}

void HTTPServer::start() {
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
    server_thread_ = std::thread(&HTTPServer::run, this);
    
    std::cout << "HTTP Server started on port " << port_ << std::endl;
}

void HTTPServer::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // 关闭服务器socket
    if (server_fd_ != -1) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
    
    // 等待服务器线程结束
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    std::cout << "HTTP Server stopped" << std::endl;
}

void HTTPServer::run() {
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
        
        // 处理客户端请求
        handle_client(client_fd);
    }
}

void HTTPServer::handle_client(int client_fd) {
    char buffer[4096];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes_read] = '\0';
    std::string request(buffer);
    
    process_http_request(client_fd, request);
    close(client_fd);
}

void HTTPServer::process_http_request(int client_fd, const std::string& request) {
    std::istringstream iss(request);
    std::string method, path, version;
    
    // 解析请求行
    iss >> method >> path >> version;
    
    // 读取所有头部
    std::map<std::string, std::string> headers;
    std::string line;
    while (std::getline(iss, line) && line != "\r") {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            // 去除首尾空白字符
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            headers[key] = value;
        }
    }
    
    // 读取请求体
    std::string body;
    if (headers.find("Content-Length") != headers.end()) {
        size_t content_length = std::stoul(headers["Content-Length"]);
        body.resize(content_length);
        iss.read(body.data(), content_length);
    }
    
    // 处理路由
    std::string response;
    
    if (path == "/" || path == "/health") {
        response = build_response(200, "application/json", 
                                 "{\"status\":\"ok\",\"message\":\"KVDB Server is running\"}");
    }
    else if (path.find("/key/") == 0) {
        std::string key = path.substr(5); // 去掉 "/key/"
        key = url_decode(key);
        
        if (method == "GET") {
            std::string value;
            if (db_.get(key, value)) {
                response = build_response(200, "text/plain", value);
            } else {
                response = build_response(404, "application/json", 
                                         "{\"error\":\"Key not found\"}");
            }
        }
        else if (method == "PUT" || method == "POST") {
            db_.put(key, body);
            response = build_response(200, "application/json", 
                                     "{\"status\":\"ok\",\"message\":\"Key updated\"}");
        }
        else if (method == "DELETE") {
            db_.del(key);
            response = build_response(200, "application/json", 
                                     "{\"status\":\"ok\",\"message\":\"Key deleted\"}");
        }
        else {
            response = build_response(405, "application/json", 
                                     "{\"error\":\"Method not allowed\"}");
        }
    }
    else {
        response = build_response(404, "application/json", 
                                 "{\"error\":\"Not found\"}");
    }
    
    // 发送响应
    send(client_fd, response.data(), response.size(), 0);
}

std::string HTTPServer::build_response(int status_code, const std::string& content_type, 
                                     const std::string& body) {
    std::string status_text;
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        default: status_text = "Internal Server Error";
    }
    
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "\r\n"
        << body;
    
    return oss.str();
}

std::string HTTPServer::url_decode(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int hex_val;
            std::istringstream iss(str.substr(i + 1, 2));
            if (iss >> std::hex >> hex_val) {
                result += static_cast<char>(hex_val);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    
    return result;
}

} // namespace network
} // namespace kvdb