# KVDB 客户端SDK

KVDB提供多语言客户端SDK，支持以下编程语言：

- **C++ SDK**: 高性能C++客户端库
- **Python SDK**: 易用的Python客户端库  
- **Java SDK**: 企业级Java客户端库
- **Go SDK**: 现代Go客户端库

## 支持的协议

- **gRPC**: 高性能RPC协议，支持流式操作
- **HTTP REST**: 简单易用的HTTP接口
- **TCP**: 原生二进制协议，最高性能

## 功能特性

- 基本KV操作 (PUT/GET/DELETE)
- 批量操作
- 范围扫描和前缀扫描
- 快照读取
- 实时订阅
- 连接池和负载均衡
- 自动重连和故障转移
- 压缩和序列化

## 快速开始

每个语言的SDK都在对应的子目录中，包含详细的使用文档和示例代码。

```
sdk/
├── cpp/          # C++ SDK
├── python/       # Python SDK  
├── java/         # Java SDK
└── go/           # Go SDK
```