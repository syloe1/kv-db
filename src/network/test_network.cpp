#include "network/tcp_server.h"
#include "network/http_server.h"
#include "db/kv_db.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace kvdb;

void test_tcp_client() {
    std::cout << "Testing TCP client..." << std::endl;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6379);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    
    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        close(sock);
        return;
    }
    
    // 测试PUT操作
    network::RequestHeader put_header;
    put_header.opcode = static_cast<uint8_t>(network::Opcode::PUT);
    std::string key = "test_key";
    std::string value = "test_value";
    put_header.key_length = key.size();
    put_header.value_length = value.size();
    
    send(sock, &put_header, sizeof(put_header), 0);
    send(sock, key.data(), key.size(), 0);
    send(sock, value.data(), value.size(), 0);
    
    network::ResponseHeader put_response;
    recv(sock, &put_response, sizeof(put_response), MSG_WAITALL);
    
    if (put_response.status == static_cast<uint8_t>(network::Status::SUCCESS)) {
        std::cout << "✓ PUT operation successful" << std::endl;
    } else {
        std::cout << "✗ PUT operation failed" << std::endl;
    }
    
    // 测试GET操作
    network::RequestHeader get_header;
    get_header.opcode = static_cast<uint8_t>(network::Opcode::GET);
    get_header.key_length = key.size();
    get_header.value_length = 0;
    
    send(sock, &get_header, sizeof(get_header), 0);
    send(sock, key.data(), key.size(), 0);
    
    network::ResponseHeader get_response;
    recv(sock, &get_response, sizeof(get_response), MSG_WAITALL);
    
    if (get_response.status == static_cast<uint8_t>(network::Status::SUCCESS)) {
        std::string received_value(get_response.value_length, '\0');
        recv(sock, received_value.data(), received_value.size(), MSG_WAITALL);
        
        if (received_value == value) {
            std::cout << "✓ GET operation successful: " << received_value << std::endl;
        } else {
            std::cout << "✗ GET operation failed: values don't match" << std::endl;
        }
    } else {
        std::cout << "✗ GET operation failed" << std::endl;
    }
    
    close(sock);
}

void test_http_client() {
    std::cout << "Testing HTTP client..." << std::endl;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    
    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        close(sock);
        return;
    }
    
    // 发送HTTP GET请求
    std::string request = 
        "GET /key/test_key HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Connection: close\r\n"
        "\r\n";
    
    send(sock, request.data(), request.size(), 0);
    
    // 读取响应
    char buffer[4096];
    ssize_t bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::string response(buffer);
        
        if (response.find("200 OK") != std::string::npos && 
            response.find("test_value") != std::string::npos) {
            std::cout << "✓ HTTP GET operation successful" << std::endl;
        } else {
            std::cout << "✗ HTTP GET operation failed" << std::endl;
            std::cout << "Response: " << response << std::endl;
        }
    }
    
    close(sock);
}

int main() {
    std::cout << "=== Network Interface Test ===" << std::endl;
    
    // 创建数据库实例
    KVDB db("network_test.log");
    
    // 启动TCP服务器
    network::TCPServer tcp_server(db, 6379);
    tcp_server.start();
    
    // 启动HTTP服务器
    network::HTTPServer http_server(db, 8080);
    http_server.start();
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 运行测试
    test_tcp_client();
    test_http_client();
    
    // 停止服务器
    tcp_server.stop();
    http_server.stop();
    
    std::cout << "=== Test Completed ===" << std::endl;
    return 0;
}