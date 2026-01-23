#!/usr/bin/env python3
"""
KVDB Python客户端基础示例
"""

import asyncio
import time
from kvdb_client import KVDBClient, ClientConfig, KeyValue, ScanOptions


def basic_operations_example():
    """基本操作示例"""
    print("=== Basic Operations Example ===")
    
    # 配置客户端
    config = ClientConfig(
        server_address="localhost:50051",
        protocol="grpc",
        connection_timeout=5.0,
        request_timeout=10.0
    )
    
    # 使用上下文管理器自动连接和断开
    with KVDBClient(config) as client:
        print("Connected to KVDB server")
        
        # PUT操作
        client.put("user:1001", "Alice")
        print("PUT user:1001 = Alice")
        
        # GET操作
        value = client.get("user:1001")
        print(f"GET user:1001 = {value}")
        
        # DELETE操作
        client.delete("user:1001")
        print("DELETE user:1001")
        
        # 验证删除
        value = client.get("user:1001")
        print(f"GET user:1001 after delete = {value}")


def batch_operations_example():
    """批量操作示例"""
    print("\n=== Batch Operations Example ===")
    
    config = ClientConfig(server_address="localhost:50051")
    
    with KVDBClient(config) as client:
        # 批量PUT
        pairs = [
            KeyValue("user:1001", "Alice"),
            KeyValue("user:1002", "Bob"),
            KeyValue("user:1003", "Charlie"),
            KeyValue("user:1004", "David")
        ]
        
        client.batch_put(pairs)
        print(f"Batch PUT {len(pairs)} pairs")
        
        # 批量GET
        keys = ["user:1001", "user:1002", "user:1003"]
        results = client.batch_get(keys)
        
        print("Batch GET results:")
        for kv in results:
            print(f"  {kv.key} = {kv.value}")


def scan_operations_example():
    """扫描操作示例"""
    print("\n=== Scan Operations Example ===")
    
    config = ClientConfig(server_address="localhost:50051")
    
    with KVDBClient(config) as client:
        # 前缀扫描
        results = client.prefix_scan("user:", limit=10)
        print("Prefix scan results:")
        for kv in results:
            print(f"  {kv.key} = {kv.value}")
        
        # 范围扫描
        scan_options = ScanOptions(
            start_key="user:1000",
            end_key="user:1999",
            limit=5
        )
        
        results = client.scan(scan_options)
        print("Range scan results:")
        for kv in results:
            print(f"  {kv.key} = {kv.value}")


def snapshot_operations_example():
    """快照操作示例"""
    print("\n=== Snapshot Operations Example ===")
    
    config = ClientConfig(server_address="localhost:50051")
    
    with KVDBClient(config) as client:
        # 创建快照
        snapshot = client.create_snapshot()
        print(f"Created snapshot: {snapshot}")
        
        # 在快照上读取数据
        value = client.get_at_snapshot("user:1001", snapshot)
        print(f"Snapshot GET user:1001 = {value}")
        
        # 修改数据
        client.put("user:1001", "Alice_Modified")
        
        # 当前读取
        current_value = client.get("user:1001")
        print(f"Current GET user:1001 = {current_value}")
        
        # 快照读取（应该是旧值）
        snapshot_value = client.get_at_snapshot("user:1001", snapshot)
        print(f"Snapshot GET user:1001 = {snapshot_value}")
        
        # 释放快照
        client.release_snapshot(snapshot)
        print("Released snapshot")


async def async_operations_example():
    """异步操作示例"""
    print("\n=== Async Operations Example ===")
    
    config = ClientConfig(server_address="localhost:50051")
    
    with KVDBClient(config) as client:
        # 异步PUT操作
        tasks = []
        for i in range(5):
            task = client.put_async(f"async:key{i}", f"value{i}")
            tasks.append(task)
        
        # 等待所有PUT操作完成
        results = await asyncio.gather(*tasks)
        print(f"Completed {len(results)} async PUT operations")
        
        # 异步GET操作
        get_tasks = []
        for i in range(5):
            task = client.get_async(f"async:key{i}")
            get_tasks.append(task)
        
        # 等待所有GET操作完成
        values = await asyncio.gather(*get_tasks)
        print("Async GET results:")
        for i, value in enumerate(values):
            print(f"  async:key{i} = {value}")


def subscription_example():
    """订阅示例"""
    print("\n=== Subscription Example ===")
    
    config = ClientConfig(server_address="localhost:50051")
    
    with KVDBClient(config) as client:
        # 定义回调函数
        def on_change(key, value, operation):
            print(f"Subscription event: {operation} {key} = {value}")
        
        # 开始订阅
        subscription_id = client.subscribe("user:*", on_change, include_deletes=True)
        print(f"Started subscription {subscription_id}")
        
        # 等待一段时间
        time.sleep(1)
        
        # 触发一些事件
        client.put("user:1005", "Eve")
        client.put("user:1006", "Frank")
        client.delete("user:1004")
        
        # 等待事件处理
        time.sleep(2)
        
        # 取消订阅
        client.cancel_subscription(subscription_id)
        print("Cancelled subscription")


def management_operations_example():
    """管理操作示例"""
    print("\n=== Management Operations Example ===")
    
    config = ClientConfig(server_address="localhost:50051")
    
    with KVDBClient(config) as client:
        # 获取统计信息
        stats = client.get_stats()
        print("Database stats:")
        for key, value in stats.items():
            print(f"  {key} = {value}")
        
        # 刷新数据
        client.flush()
        print("Flushed data to disk")
        
        # 压缩数据
        client.compact()
        print("Compacted data")


def http_client_example():
    """HTTP客户端示例"""
    print("\n=== HTTP Client Example ===")
    
    config = ClientConfig(
        server_address="localhost:50051",
        protocol="http",
        http_base_url="http://localhost:8080"
    )
    
    try:
        with KVDBClient(config) as client:
            # 基本操作
            client.put("http:test", "HTTP Value")
            value = client.get("http:test")
            print(f"HTTP GET result: {value}")
            
            # 批量操作
            pairs = [KeyValue("http:1", "value1"), KeyValue("http:2", "value2")]
            client.batch_put(pairs)
            
            results = client.batch_get(["http:1", "http:2"])
            print("HTTP batch results:")
            for kv in results:
                print(f"  {kv.key} = {kv.value}")
                
    except Exception as e:
        print(f"HTTP client error (server may not be running): {e}")


def main():
    """主函数"""
    try:
        basic_operations_example()
        batch_operations_example()
        scan_operations_example()
        snapshot_operations_example()
        
        # 异步操作需要事件循环
        asyncio.run(async_operations_example())
        
        subscription_example()
        management_operations_example()
        http_client_example()
        
    except Exception as e:
        print(f"Error: {e}")
        print("Make sure KVDB server is running on localhost:50051")


if __name__ == "__main__":
    main()