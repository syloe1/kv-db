# KVDB C++ SDK

高性能的KVDB C++客户端库，支持gRPC、HTTP和TCP协议。

## 特性

- 支持所有KVDB操作（PUT/GET/DELETE/SCAN等）
- 异步操作支持
- 连接池和负载均衡
- 自动重连和故障转移
- 实时订阅功能
- 快照读取
- 批量操作优化

## 依赖

- C++17或更高版本
- gRPC (>= 1.50.0)
- Protocol Buffers (>= 3.21.0)
- CMake (>= 3.16)

## 安装

### 从源码构建

```bash
git clone https://github.com/kvdb/cpp-client.git
cd cpp-client
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

### 使用包管理器

```bash
# Ubuntu/Debian
sudo apt-get install libkvdb-client-dev

# CentOS/RHEL
sudo yum install kvdb-client-devel

# macOS
brew install kvdb-client
```

## 快速开始

```cpp
#include "kvdb_client.h"
#include <iostream>

using namespace kvdb::client;

int main() {
    // 配置客户端
    ClientConfig config;
    config.server_address = "localhost:50051";
    config.protocol = "grpc";
    
    // 创建客户端
    auto client = ClientFactory::create_client(config);
    
    // 连接
    auto result = client->connect();
    if (!result.success) {
        std::cerr << "Connection failed: " << result.error_message << std::endl;
        return 1;
    }
    
    // 基本操作
    client->put("key1", "value1");
    auto get_result = client->get("key1");
    if (get_result.success) {
        std::cout << "key1 = " << get_result.value << std::endl;
    }
    
    // 断开连接
    client->disconnect();
    return 0;
}
```

## API文档

### 客户端配置

```cpp
ClientConfig config;
config.server_address = "localhost:50051";  // 服务器地址
config.protocol = "grpc";                   // 协议类型
config.connection_timeout_ms = 5000;        // 连接超时
config.request_timeout_ms = 30000;          // 请求超时
config.max_retries = 3;                     // 最大重试次数
config.enable_compression = true;           // 启用压缩
```

### 基本操作

```cpp
// PUT操作
auto result = client->put("key", "value");
if (!result.success) {
    std::cerr << "PUT failed: " << result.error_message << std::endl;
}

// GET操作
auto result = client->get("key");
if (result.success) {
    std::cout << "Value: " << result.value << std::endl;
} else {
    std::cerr << "GET failed: " << result.error_message << std::endl;
}

// DELETE操作
auto result = client->del("key");
if (!result.success) {
    std::cerr << "DELETE failed: " << result.error_message << std::endl;
}
```

### 异步操作

```cpp
// 异步PUT
auto future = client->put_async("key", "value");
auto result = future.get(); // 等待完成

// 异步GET
auto future = client->get_async("key");
auto result = future.get();
```

### 批量操作

```cpp
// 批量PUT
std::vector<KeyValue> pairs = {
    {"key1", "value1"},
    {"key2", "value2"},
    {"key3", "value3"}
};
auto result = client->batch_put(pairs);

// 批量GET
std::vector<std::string> keys = {"key1", "key2", "key3"};
auto result = client->batch_get(keys);
if (result.success) {
    for (const auto& kv : result.value) {
        std::cout << kv.key << " = " << kv.value << std::endl;
    }
}
```

### 扫描操作

```cpp
// 前缀扫描
auto result = client->prefix_scan("user:", 100);
if (result.success) {
    for (const auto& kv : result.value) {
        std::cout << kv.key << " = " << kv.value << std::endl;
    }
}

// 范围扫描
ScanOptions options;
options.start_key = "user:1000";
options.end_key = "user:2000";
options.limit = 50;

auto result = client->scan(options);
```

### 快照操作

```cpp
// 创建快照
auto snapshot_result = client->create_snapshot();
if (snapshot_result.success) {
    auto snapshot = snapshot_result.value;
    
    // 在快照上读取
    auto result = client->get_at_snapshot("key", snapshot);
    
    // 释放快照
    client->release_snapshot(snapshot);
}
```

### 实时订阅

```cpp
// 订阅键变化
auto subscription_result = client->subscribe("user:*", 
    [](const std::string& key, const std::string& value, const std::string& operation) {
        std::cout << "Event: " << operation << " " << key << " = " << value << std::endl;
    }, true);

if (subscription_result.success) {
    auto subscription = subscription_result.value;
    
    // 等待事件...
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 取消订阅
    subscription->cancel();
}
```

## 编译选项

```cmake
# CMakeLists.txt
find_package(PkgConfig REQUIRED)
pkg_check_modules(KVDB_CLIENT REQUIRED kvdb-client)

target_link_libraries(your_target ${KVDB_CLIENT_LIBRARIES})
target_include_directories(your_target PRIVATE ${KVDB_CLIENT_INCLUDE_DIRS})
target_compile_options(your_target PRIVATE ${KVDB_CLIENT_CFLAGS_OTHER})
```

## 性能优化

- 使用连接池减少连接开销
- 启用压缩减少网络传输
- 使用批量操作提高吞吐量
- 合理设置超时时间
- 使用异步操作提高并发性

## 错误处理

所有操作都返回`Result<T>`类型，包含成功标志和错误信息：

```cpp
auto result = client->get("key");
if (!result.success) {
    std::cerr << "Operation failed: " << result.error_message << std::endl;
    // 处理错误...
}
```

## 线程安全

KVDB C++客户端是线程安全的，可以在多线程环境中使用。

## 许可证

MIT License