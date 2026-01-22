#pragma once

#include "db/kv_db.h"
#include "network/protocol.h"
#include <thread>
#include <mutex>
#include <vector>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace kvdb {
namespace network {

class TCPServer {
public:
    TCPServer(KVDB& db, uint16_t port = 6379);
    ~TCPServer();
    
    void start();
    void stop();
    
    bool is_running() const { return running_; }
    uint16_t port() const { return port_; }

private:
    void run();
    void handle_client(int client_fd);
    bool process_request(int client_fd, const RequestHeader& header, 
                        const std::string& key, const std::string& value);
    
    bool send_response(int client_fd, Status status, const std::string& value = "");
    
    KVDB& db_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    int server_fd_{-1};
    std::thread server_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex clients_mutex_;
};

} // namespace network
} // namespace kvdb