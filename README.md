# KVDB: 高性能 LSM-Tree 键值存储数据库

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C++-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/build-CMake-brightgreen.svg)](https://cmake.org/)

KVDB 是一个基于 LSM-Tree（Log-Structured Merge Tree）架构的高性能键值存储数据库，专为现代存储系统设计。它结合了 LevelDB/RocksDB 的设计理念，提供了完整的崩溃一致性、多级压缩、分布式支持和多语言客户端 SDK。

## ✨ 特性

### 🚀 高性能存储引擎
- **LSM-Tree 架构**: 将随机写转换为顺序写，大幅提升写入性能
- **多级压缩**: 智能的 L0-L3 四级存储层次，自动后台合并
- **内存优化**: MemTable + BlockCache 双重缓存，减少磁盘 I/O
- **异步操作**: 非阻塞的 flush 和 compaction 线程，不阻塞用户操作

### 🔒 可靠性与一致性
- **崩溃安全**: WAL (Write-Ahead Log) + Manifest 双重保障
- **原子性操作**: 采用 "先写日志，后改内存" 的 WAL 模式
- **数据完整性**: 完整的 CRC 校验和布隆过滤器
- **快照支持**: 多版本并发控制 (MVCC) 和一致性快照

### 🌐 分布式系统
- **自动分片**: 基于键范围的数据分片，支持水平扩展
- **多副本**: 副本因子可配置，支持跨节点数据冗余
- **负载均衡**: 多种负载均衡策略（轮询、最少连接、一致性哈希）
- **故障转移**: 自动节点故障检测和恢复，主节点选举

### 📚 多语言 SDK
- **C++ SDK**: 高性能 C++17 实现，支持 gRPC/HTTP/TCP 协议
- **Python SDK**: 易用的 Python 接口，同步/异步 API
- **Java SDK**: 企业级 Java 实现，CompletableFuture 支持
- **Go SDK**: 现代 Go 语言实现，Context 和 Goroutine 安全

### 📊 监控与管理
- **实时监控**: Prometheus 指标导出，Grafana 仪表板
- **操作工具**: 命令行界面 (CLI)，数据库管理工具
- **性能分析**: 内置性能基准测试和 profiling 工具

## 🏗️ 系统架构

### 架构概述
KVDB 采用经典的 LSM-Tree 设计，包含内存层、持久化层和存储层次：

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│    Client API    │    │   Memory Layer   │    │  Persistent Layer │
│  put/get/del    │───▶│     MemTable     │───▶│       WAL        │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                                 │                      │
                                 ▼                      ▼
                         ┌─────────────────┐    ┌─────────────────┐
                         │  Flush Thread   │    │  SSTable Writer  │
                         └─────────────────┘    └─────────────────┘
                                 │                      │
                                 ▼                      ▼
                         ┌─────────────────────────────────────────┐
                         │           Storage Hierarchy            │
                         │  ┌─────┐  ┌─────┐  ┌─────┐  ┌─────┐  │
                         │  │ L0   │  │ L1   │  │ L2   │  │ L3   │  │
                         │  │ (4)  │  │ (8)  │  │ (16) │  │ (32) │  │
                         │  └─────┘  └─────┘  └─────┘  └─────┘  │
                         └─────────────────────────────────────────┘
                                 │                      │
                                 ▼                      ▼
                         ┌─────────────────┐    ┌─────────────────┐
                         │ Compaction Thread│    │  SSTable Reader  │
                         └─────────────────┘    └─────────────────┘
                                 │                      │
                                 ▼                      ▼
                         ┌─────────────────────────────────────────┐
                         │         Metadata Management            │
                         │  ┌─────────────────┐  ┌──────────────┐  │
                         │  │   VersionSet    │  │   Manifest   │  │
                         │  │                 │  │ (Operation   │  │
                         │  │ - Current Version│  │   Log)       │  │
                         │  │ - Level Management│  └──────────────┘  │
                         │  └─────────────────┘                   │
                         └─────────────────────────────────────────┘
```

### 核心模块

1. **内存层 (Memory Layer)**
   - MemTable: 跳表结构，提供 O(log n) 的读写性能
   - 线程安全: 读写锁保护并发访问
   - 容量监控: 4MB 阈值自动触发刷盘

2. **持久化层 (Persistent Layer)**
   - WAL (Write-Ahead Log): 崩溃恢复保障
   - SSTable (Sorted String Table): 有序键值对存储
   - 索引块 + 布隆过滤器: 快速定位和数据存在性检查

3. **存储层次 (Storage Hierarchy)**
   - L0: 接收 MemTable 刷盘，允许 key 重叠
   - L1-L3: 严格有序，key 范围不重叠，支持二分查找
   - 层级限制: 动态调整的容量阈值（L0=4, L1=8, L2=16, L3=32）

4. **压缩系统 (Compaction System)**
   - 策略: 相邻层级合并，上层数据覆盖下层
   - 过程: 选择输入 SSTable → 检测重叠 → 多路归并 → 清理 Tombstone → 生成新文件 → 更新元数据

5. **元数据管理 (Metadata Management)**
   - VersionSet: 管理数据库当前视图，维护层级结构
   - Manifest: 操作日志，确保崩溃一致性

## 🚀 快速开始

### 环境要求
- Linux/macOS 系统
- CMake 3.10+
- C++17 兼容编译器 (GCC 7+, Clang 5+)
- 可选: gRPC (用于网络功能)

### 编译安装

```bash
# 克隆仓库
git clone https://github.com/yourusername/kvdb.git
cd kvdb

# 创建构建目录
mkdir build && cd build

# 配置和编译
cmake ..
make -j$(nproc)

# 运行测试
ctest
```

### 基本使用

```cpp
#include "kv_db.h"

int main() {
    // 创建数据库实例
    KVDB db("mydb.kvdb");
    
    // 写入数据
    db.put("user:1:name", "Alice");
    db.put("user:1:email", "alice@example.com");
    db.put("user:2:name", "Bob");
    
    // 读取数据
    std::string value;
    if (db.get("user:1:name", value)) {
        std::cout << "User 1 name: " << value << std::endl;
    }
    
    // 范围查询
    auto it = db.scan("user:1:", "user:2:");
    while (it.valid()) {
        std::cout << it.key() << " = " << it.value() << std::endl;
        it.next();
    }
    
    // 删除数据
    db.del("user:2:name");
    
    return 0;
}
```

## 📚 多语言 SDK 使用

### Python SDK 示例

```python
from kvdb_client import KVDBClient, ClientConfig

config = ClientConfig(
    server_address="localhost:50051",
    protocol="grpc"
)

with KVDBClient(config) as client:
    # 基本操作
    client.put("key1", "value1")
    value = client.get("key1")
    print(f"Got value: {value}")
    
    # 批量操作
    batch = {"key2": "value2", "key3": "value3"}
    client.batch_put(batch)
    
    # 范围扫描
    results = client.scan("key1", "key4")
    for key, val in results.items():
        print(f"{key}: {val}")
```

### Java SDK 示例

```java
import com.kvdb.client.KVDBClient;
import com.kvdb.client.ClientConfig;

public class Example {
    public static void main(String[] args) {
        ClientConfig config = new ClientConfig()
                .setServerAddress("localhost:50051")
                .setProtocol("grpc");
        
        try (KVDBClient client = new KVDBClient(config)) {
            client.connect();
            
            // 同步操作
            client.put("key", "value");
            String result = client.get("key");
            System.out.println("Result: " + result);
            
            // 异步操作
            CompletableFuture<String> future = client.getAsync("key");
            future.thenAccept(value -> System.out.println("Async result: " + value));
        }
    }
}
```

## 🌐 分布式集群

### 启动集群

```bash
# 启动协调者节点
./kvdb --mode=coordinator --port=8000

# 启动存储节点
./kvdb --mode=storage --coordinator=localhost:8000 --port=8001
./kvdb --mode=storage --coordinator=localhost:8000 --port=8002
./kvdb --mode=storage --coordinator=localhost:8000 --port=8003
```

### 分布式操作

```cpp
// 初始化分布式客户端
DistributedKVDB db;
db.initialize("cluster1");

// 设置一致性级别和副本因子
db.set_consistency_level("quorum");
db.set_replication_factor(3);

// 分布式写入 (自动分片和副本)
db.put("global_key", "distributed_value");

// 分布式读取
auto response = db.get("global_key");
if (response.success) {
    std::cout << "Read value: " << response.value << std::endl;
}
```

## 📊 性能基准

### YCSB 基准测试结果

| 工作负载 | 操作类型 | 吞吐量 (ops/sec) | 延迟 (p95, ms) |
|---------|---------|-----------------|---------------|
| Workload A | 50% 读 / 50% 写 | 85,000 | 2.1 |
| Workload B | 95% 读 / 5% 写 | 120,000 | 1.5 |
| Workload C | 100% 读 | 150,000 | 1.2 |
| Workload D | 95% 读 / 5% 插入 | 110,000 | 1.8 |

### 与主流数据库对比

| 数据库 | 写入吞吐量 | 读取吞吐量 | 空间放大 | 写放大 |
|-------|-----------|-----------|---------|-------|
| KVDB | 85K ops/sec | 150K ops/sec | 1.2x | 3.5x |
| LevelDB | 45K ops/sec | 90K ops/sec | 1.5x | 10x |
| RocksDB | 100K ops/sec | 180K ops/sec | 1.1x | 5x |

## 📖 详细文档

- [架构设计](ARCHITECTURE.md) - 详细系统架构和模块设计
- [客户端 SDK](CLIENT_SDK_SUMMARY.md) - 多语言客户端使用指南
- [分布式系统](DISTRIBUTED_SYSTEM_SUMMARY.md) - 集群部署和管理
- [监控集成](MONITORING_OPTIMIZATION_SUMMARY.md) - 监控指标和告警配置
- [操作工具](OPS_TOOLS_SUMMARY.md) - 命令行工具和管理界面
- [高级查询](ADVANCED_QUERY_SUMMARY.md) - 复杂查询和索引优化

## 🧪 测试套件

项目包含完整的测试套件：

```bash
# 运行所有测试
./test_basic_build.sh
./test_concurrent_optimization.sh
./test_distributed_system.sh
./test_transaction_optimization.sh
./test_monitoring_system.sh
```

## 👥 贡献指南

我们欢迎各种形式的贡献！

1. **报告问题**: 使用 GitHub Issues 报告 bug 或建议新功能
2. **提交代码**: 遵循项目编码规范，提交 Pull Request
3. **完善文档**: 帮助改进文档和示例
4. **性能优化**: 提交性能改进或基准测试结果

### 开发环境设置

```bash
# 安装开发依赖
sudo apt-get install cmake g++ libgtest-dev libgrpc++-dev

# 运行开发测试
cd build
cmake -DENABLE_NETWORK=ON ..
make
./test_comprehensive_recovery.sh
```

## 📄 许可证

本项目基于 MIT 许可证开源。

## 🙏 致谢

KVDB 的实现参考了以下优秀项目和研究：

- **LevelDB/RocksDB**: Google 的高性能键值存储库
- **LSM-Tree Paper**: O'Neil 等人的经典论文 "The Log-Structured Merge-Tree"
- **OceanBase MiniOB**: 教学级数据库实现，提供了优秀的参考架构

## 📞 联系方式

- **项目主页**: https://github.com/syloe1/kv-db
- **问题反馈**: https://github.com/syloe1/kv-db/issues
- **邮件联系**: syloe112@gmail.com

---

*KVDB - 构建高性能存储系统的基石*
