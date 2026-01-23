#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <future>
#include <map>

namespace kvdb {
namespace client {

// 客户端配置
struct ClientConfig {
    std::string server_address = "localhost:50051";
    std::string protocol = "grpc";  // grpc, http, tcp
    int connection_timeout_ms = 5000;
    int request_timeout_ms = 30000;
    int max_retries = 3;
    bool enable_compression = true;
    std::string compression_type = "gzip";
    
    // 连接池配置
    int max_connections = 10;
    int min_connections = 2;
    int connection_idle_timeout_ms = 60000;
};

// 操作结果
template<typename T>
struct Result {
    bool success;
    T value;
    std::string error_message;
    
    Result(bool s, T v, const std::string& err = "") 
        : success(s), value(std::move(v)), error_message(err) {}
    
    static Result<T> ok(T value) {
        return Result<T>(true, std::move(value));
    }
    
    static Result<T> error(const std::string& message) {
        return Result<T>(false, T{}, message);
    }
};

// 键值对
struct KeyValue {
    std::string key;
    std::string value;
    
    KeyValue() = default;
    KeyValue(std::string k, std::string v) : key(std::move(k)), value(std::move(v)) {}
};

// 扫描选项
struct ScanOptions {
    std::string start_key;
    std::string end_key;
    std::string prefix;
    int limit = 1000;
    bool reverse = false;
};

// 快照句柄
class Snapshot {
public:
    explicit Snapshot(uint64_t id) : id_(id) {}
    uint64_t id() const { return id_; }
    
private:
    uint64_t id_;
};

// 订阅回调
using SubscriptionCallback = std::function<void(const std::string& key, 
                                               const std::string& value, 
                                               const std::string& operation)>;

// 订阅句柄
class Subscription {
public:
    virtual ~Subscription() = default;
    virtual void cancel() = 0;
    virtual bool is_active() const = 0;
};

// KVDB客户端接口
class KVDBClient {
public:
    virtual ~KVDBClient() = default;
    
    // 连接管理
    virtual Result<bool> connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
    
    // 基本操作
    virtual Result<bool> put(const std::string& key, const std::string& value) = 0;
    virtual Result<std::string> get(const std::string& key) = 0;
    virtual Result<bool> del(const std::string& key) = 0;
    
    // 异步操作
    virtual std::future<Result<bool>> put_async(const std::string& key, const std::string& value) = 0;
    virtual std::future<Result<std::string>> get_async(const std::string& key) = 0;
    virtual std::future<Result<bool>> del_async(const std::string& key) = 0;
    
    // 批量操作
    virtual Result<bool> batch_put(const std::vector<KeyValue>& pairs) = 0;
    virtual Result<std::vector<KeyValue>> batch_get(const std::vector<std::string>& keys) = 0;
    
    // 扫描操作
    virtual Result<std::vector<KeyValue>> scan(const ScanOptions& options) = 0;
    virtual Result<std::vector<KeyValue>> prefix_scan(const std::string& prefix, int limit = 1000) = 0;
    
    // 快照操作
    virtual Result<std::shared_ptr<Snapshot>> create_snapshot() = 0;
    virtual Result<bool> release_snapshot(std::shared_ptr<Snapshot> snapshot) = 0;
    virtual Result<std::string> get_at_snapshot(const std::string& key, std::shared_ptr<Snapshot> snapshot) = 0;
    
    // 管理操作
    virtual Result<bool> flush() = 0;
    virtual Result<bool> compact() = 0;
    virtual Result<std::map<std::string, std::string>> get_stats() = 0;
    
    // 实时订阅
    virtual Result<std::shared_ptr<Subscription>> subscribe(const std::string& pattern, 
                                                           SubscriptionCallback callback,
                                                           bool include_deletes = false) = 0;
};

// 客户端工厂
class ClientFactory {
public:
    static std::unique_ptr<KVDBClient> create_grpc_client(const ClientConfig& config);
    static std::unique_ptr<KVDBClient> create_http_client(const ClientConfig& config);
    static std::unique_ptr<KVDBClient> create_tcp_client(const ClientConfig& config);
    static std::unique_ptr<KVDBClient> create_client(const ClientConfig& config);
};

} // namespace client
} // namespace kvdb