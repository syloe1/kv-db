#include "src/network/tcp_server.h"
#include "src/network/http_server.h"
#include "src/db/kv_db.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace kvdb;

int main() {
    std::cout << "=== KVDB 网络接口测试 ===" << std::endl;
    
    // 1. 创建数据库实例
    KVDB db("network_test.log");
    
    // 2. 启动TCP服务器 (端口6379)
    network::TCPServer tcp_server(db, 6379);
    tcp_server.start();
    
    // 3. 启动HTTP服务器 (端口8080)  
    network::HTTPServer http_server(db, 8080);
    http_server.start();
    
    std::cout << "✅ TCP服务器已启动: localhost:6379" << std::endl;
    std::cout << "✅ HTTP服务器已启动: http://localhost:8080" << std::endl;
    std::cout << "\n📋 测试方法:" << std::endl;
    std::cout << "1. 使用 curl 测试 HTTP API:" << std::endl;
    std::cout << "   curl -X PUT http://localhost:8080/key/test_key -d \"hello world\"" << std::endl;
    std::cout << "   curl http://localhost:8080/key/test_key" << std::endl;
    std::cout << "\n2. 使用 telnet/netcat 测试 TCP 协议:" << std::endl;
    std::cout << "   echo -e '\\x4B\\x56\\x44\\x42\\x01\\x08\\x00\\x00\\x00\\x0B\\x00\\x00test_keyhello world' | nc localhost 6379" << std::endl;
    std::cout << "\n3. 或者使用浏览器访问: http://localhost:8080/key/test_key" << std::endl;
    
    std::cout << "\n⏳ 服务器运行中...按 Ctrl+C 停止" << std::endl;
    
    // 保持服务器运行
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}