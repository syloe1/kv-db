#pragma once

#include "db/kv_db.h"
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <map>

namespace kvdb {
namespace network {

class HTTPServer {
public:
    HTTPServer(KVDB& db, uint16_t port = 8080);
    ~HTTPServer();
    
    void start();
    void stop();
    
    bool is_running() const { return running_; }
    uint16_t port() const { return port_; }

private:
    void run();
    void handle_client(int client_fd);
    void process_http_request(int client_fd, const std::string& request);
    
    std::string build_response(int status_code, const std::string& content_type, 
                             const std::string& body);
    std::string url_decode(const std::string& str);
    
    KVDB& db_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    int server_fd_{-1};
    std::thread server_thread_;
};

} // namespace network
} // namespace kvdb