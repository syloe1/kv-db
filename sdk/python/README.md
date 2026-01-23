# KVDB Python SDK

易用的KVDB Python客户端库，支持同步和异步操作。

## 特性

- 支持所有KVDB操作
- 同步和异步API
- 上下文管理器支持
- 实时订阅功能
- 快照读取
- 批量操作优化
- HTTP和gRPC协议支持

## 安装

```bash
pip install kvdb-client
```

或从源码安装：

```bash
git clone https://github.com/kvdb/python-client.git
cd python-client
pip install -e .
```

## 快速开始

### 同步API

```python
from kvdb_client import KVDBClient, ClientConfig

# 配置客户端
config = ClientConfig(
    server_address="localhost:50051",
    protocol="grpc",
    connection_timeout=5.0,
    request_timeout=10.0
)

# 使用上下文管理器
with KVDBClient(config) as client:
    # 基本操作
    client.put("key1", "value1")
    value = client.get("key1")
    print(f"key1 = {value}")
    
    # 删除
    client.delete("key1")
```

### 异步API

```python
import asyncio
from kvdb_client import KVDBClient, ClientConfig

async def main():
    config = ClientConfig(server_address="localhost:50051")
    
    with KVDBClient(config) as client:
        # 异步操作
        await client.put_async("key1", "value1")
        value = await client.get_async("key1")
        print(f"key1 = {value}")
        
        await client.delete_async("key1")

# 运行异步代码
asyncio.run(main())
```

## API文档

### 客户端配置

```python
from kvdb_client import ClientConfig

config = ClientConfig(
    server_address="localhost:50051",  # 服务器地址
    protocol="grpc",                   # 协议类型 (grpc/http)
    connection_timeout=5.0,            # 连接超时(秒)
    request_timeout=30.0,              # 请求超时(秒)
    max_retries=3,                     # 最大重试次数
    enable_compression=True,           # 启用压缩
    compression_type="gzip"            # 压缩类型
)
```

### 基本操作

```python
# PUT操作
success = client.put("key", "value")
if not success:
    print("PUT failed")

# GET操作
value = client.get("key")
if value is not None:
    print(f"Value: {value}")
else:
    print("Key not found")

# DELETE操作
success = client.delete("key")
if not success:
    print("DELETE failed")
```

### 批量操作

```python
from kvdb_client import KeyValue

# 批量PUT
pairs = [
    KeyValue("key1", "value1"),
    KeyValue("key2", "value2"),
    KeyValue("key3", "value3")
]
success = client.batch_put(pairs)

# 批量GET
keys = ["key1", "key2", "key3"]
results = client.batch_get(keys)
for kv in results:
    print(f"{kv.key} = {kv.value}")
```

### 扫描操作

```python
from kvdb_client import ScanOptions

# 前缀扫描
results = client.prefix_scan("user:", limit=100)
for kv in results:
    print(f"{kv.key} = {kv.value}")

# 范围扫描
options = ScanOptions(
    start_key="user:1000",
    end_key="user:2000",
    limit=50
)
results = client.scan(options)
```

### 快照操作

```python
# 创建快照
snapshot = client.create_snapshot()
print(f"Created snapshot: {snapshot.snapshot_id}")

# 在快照上读取
value = client.get_at_snapshot("key", snapshot)
print(f"Snapshot value: {value}")

# 释放快照
client.release_snapshot(snapshot)
```

### 实时订阅

```python
import time

# 定义回调函数
def on_change(key, value, operation):
    print(f"Event: {operation} {key} = {value}")

# 开始订阅
subscription_id = client.subscribe("user:*", on_change, include_deletes=True)

# 等待事件
time.sleep(10)

# 取消订阅
client.cancel_subscription(subscription_id)
```

### 异步操作

```python
import asyncio

async def async_operations():
    # 异步PUT
    await client.put_async("key", "value")
    
    # 异步GET
    value = await client.get_async("key")
    
    # 异步DELETE
    await client.delete_async("key")
    
    # 并发操作
    tasks = [
        client.put_async(f"key{i}", f"value{i}")
        for i in range(10)
    ]
    await asyncio.gather(*tasks)
```

### 管理操作

```python
# 获取统计信息
stats = client.get_stats()
print(f"Memtable size: {stats['memtable_size']}")
print(f"Cache hit rate: {stats['cache_hit_rate']}")

# 刷新数据
client.flush()

# 压缩数据
client.compact()
```

## HTTP客户端

```python
# 使用HTTP协议
config = ClientConfig(
    server_address="localhost:50051",
    protocol="http",
    http_base_url="http://localhost:8080"
)

with KVDBClient(config) as client:
    client.put("key", "value")
    value = client.get("key")
```

## 错误处理

```python
from kvdb_client import KVDBError, ConnectionError, TimeoutError

try:
    client.put("key", "value")
except ConnectionError as e:
    print(f"Connection error: {e}")
except TimeoutError as e:
    print(f"Timeout error: {e}")
except KVDBError as e:
    print(f"KVDB error: {e}")
```

## 配置示例

### 生产环境配置

```python
config = ClientConfig(
    server_address="kvdb.example.com:50051",
    protocol="grpc",
    connection_timeout=10.0,
    request_timeout=60.0,
    max_retries=5,
    enable_compression=True,
    max_connections=20,
    connection_idle_timeout=300.0
)
```

### 开发环境配置

```python
config = ClientConfig(
    server_address="localhost:50051",
    protocol="grpc",
    connection_timeout=5.0,
    request_timeout=30.0,
    max_retries=3
)
```

## 性能优化

- 使用批量操作减少网络往返
- 启用压缩减少传输数据量
- 合理设置连接池大小
- 使用异步操作提高并发性
- 复用客户端连接

## 日志配置

```python
import logging

# 启用调试日志
logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger('kvdb_client')
logger.setLevel(logging.DEBUG)
```

## 许可证

MIT License