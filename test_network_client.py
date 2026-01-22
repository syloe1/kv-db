#!/usr/bin/env python3
"""
KVDB 网络接口测试客户端
支持 gRPC 和 WebSocket 接口测试
"""

import asyncio
import websockets
import json
import time
import sys

class WebSocketClient:
    def __init__(self, uri="ws://localhost:8080"):
        self.uri = uri
        self.websocket = None
    
    async def connect(self):
        """连接到 WebSocket 服务器"""
        try:
            self.websocket = await websockets.connect(self.uri)
            print(f"Connected to WebSocket server at {self.uri}")
            return True
        except Exception as e:
            print(f"Failed to connect: {e}")
            return False
    
    async def send_request(self, method, params=None):
        """发送请求到服务器"""
        if not self.websocket:
            print("Not connected to server")
            return None
        
        request = {
            "method": method,
            "params": params or {}
        }
        
        try:
            await self.websocket.send(json.dumps(request))
            response = await self.websocket.recv()
            return json.loads(response)
        except Exception as e:
            print(f"Request failed: {e}")
            return None
    
    async def close(self):
        """关闭连接"""
        if self.websocket:
            await self.websocket.close()
            print("Connection closed")

async def test_basic_operations():
    """测试基本操作"""
    client = WebSocketClient()
    
    if not await client.connect():
        return
    
    print("\n=== Testing Basic Operations ===")
    
    # 测试 PUT
    print("1. Testing PUT operations...")
    for i in range(5):
        key = f"test:key_{i}"
        value = f"value_{i}"
        response = await client.send_request("put", {"key": key, "value": value})
        print(f"PUT {key} = {value}: {response}")
    
    # 测试 GET
    print("\n2. Testing GET operations...")
    for i in range(5):
        key = f"test:key_{i}"
        response = await client.send_request("get", {"key": key})
        print(f"GET {key}: {response}")
    
    # 测试 DELETE
    print("\n3. Testing DELETE operations...")
    response = await client.send_request("delete", {"key": "test:key_2"})
    print(f"DELETE test:key_2: {response}")
    
    # 验证删除
    response = await client.send_request("get", {"key": "test:key_2"})
    print(f"GET test:key_2 (after delete): {response}")
    
    await client.close()

async def test_batch_operations():
    """测试批量操作"""
    client = WebSocketClient()
    
    if not await client.connect():
        return
    
    print("\n=== Testing Batch Operations ===")
    
    # 批量 PUT
    print("1. Testing Batch PUT...")
    pairs = [
        {"key": f"batch:key_{i}", "value": f"batch_value_{i}"}
        for i in range(10)
    ]
    response = await client.send_request("batch_put", {"pairs": pairs})
    print(f"Batch PUT result: {response}")
    
    # 批量 GET
    print("\n2. Testing Batch GET...")
    keys = [f"batch:key_{i}" for i in range(10)]
    response = await client.send_request("batch_get", {"keys": keys})
    print(f"Batch GET result: {response}")
    
    await client.close()

async def test_scan_operations():
    """测试扫描操作"""
    client = WebSocketClient()
    
    if not await client.connect():
        return
    
    print("\n=== Testing Scan Operations ===")
    
    # 范围扫描
    print("1. Testing Range Scan...")
    response = await client.send_request("scan", {
        "start_key": "batch:key_0",
        "end_key": "batch:key_9",
        "limit": 5
    })
    print(f"Range scan result: {response}")
    
    # 前缀扫描
    print("\n2. Testing Prefix Scan...")
    response = await client.send_request("prefix_scan", {
        "prefix": "batch:",
        "limit": 5
    })
    print(f"Prefix scan result: {response}")
    
    await client.close()

async def test_snapshot_operations():
    """测试快照操作"""
    client = WebSocketClient()
    
    if not await client.connect():
        return
    
    print("\n=== Testing Snapshot Operations ===")
    
    # 创建快照
    print("1. Creating snapshot...")
    response = await client.send_request("create_snapshot")
    print(f"Create snapshot result: {response}")
    
    if response and response.get("success"):
        snapshot_id = response["data"]["snapshot_id"]
        
        # 修改数据
        await client.send_request("put", {"key": "snapshot_test", "value": "new_value"})
        
        # 从快照读取
        print("2. Reading from snapshot...")
        response = await client.send_request("get_at_snapshot", {
            "key": "snapshot_test",
            "snapshot_id": snapshot_id
        })
        print(f"Get at snapshot result: {response}")
        
        # 释放快照
        print("3. Releasing snapshot...")
        response = await client.send_request("release_snapshot", {
            "snapshot_id": snapshot_id
        })
        print(f"Release snapshot result: {response}")
    
    await client.close()

async def test_management_operations():
    """测试管理操作"""
    client = WebSocketClient()
    
    if not await client.connect():
        return
    
    print("\n=== Testing Management Operations ===")
    
    # 获取统计信息
    print("1. Getting statistics...")
    response = await client.send_request("get_stats")
    print(f"Statistics: {response}")
    
    # 刷盘
    print("\n2. Flushing...")
    response = await client.send_request("flush")
    print(f"Flush result: {response}")
    
    # 压缩
    print("\n3. Compacting...")
    response = await client.send_request("compact")
    print(f"Compact result: {response}")
    
    # 设置压缩策略
    print("\n4. Setting compaction strategy...")
    response = await client.send_request("set_compaction_strategy", {
        "strategy": "TIERED"
    })
    print(f"Set compaction strategy result: {response}")
    
    await client.close()

async def performance_test():
    """性能测试"""
    client = WebSocketClient()
    
    if not await client.connect():
        return
    
    print("\n=== Performance Test ===")
    
    # 写入性能测试
    print("1. Write performance test...")
    start_time = time.time()
    
    for i in range(1000):
        key = f"perf:key_{i}"
        value = f"performance_test_value_{i}"
        await client.send_request("put", {"key": key, "value": value})
        
        if i % 100 == 0:
            print(f"  Processed {i} writes...")
    
    write_time = time.time() - start_time
    print(f"Write performance: {1000/write_time:.2f} ops/sec")
    
    # 读取性能测试
    print("\n2. Read performance test...")
    start_time = time.time()
    
    for i in range(1000):
        key = f"perf:key_{i}"
        await client.send_request("get", {"key": key})
        
        if i % 100 == 0:
            print(f"  Processed {i} reads...")
    
    read_time = time.time() - start_time
    print(f"Read performance: {1000/read_time:.2f} ops/sec")
    
    await client.close()

async def main():
    """主函数"""
    print("KVDB WebSocket Client Test")
    print("=" * 40)
    
    if len(sys.argv) > 1:
        test_type = sys.argv[1]
        
        if test_type == "basic":
            await test_basic_operations()
        elif test_type == "batch":
            await test_batch_operations()
        elif test_type == "scan":
            await test_scan_operations()
        elif test_type == "snapshot":
            await test_snapshot_operations()
        elif test_type == "management":
            await test_management_operations()
        elif test_type == "performance":
            await performance_test()
        elif test_type == "all":
            await test_basic_operations()
            await test_batch_operations()
            await test_scan_operations()
            await test_snapshot_operations()
            await test_management_operations()
        else:
            print(f"Unknown test type: {test_type}")
            print("Available tests: basic, batch, scan, snapshot, management, performance, all")
    else:
        print("Usage: python3 test_network_client.py <test_type>")
        print("Available tests: basic, batch, scan, snapshot, management, performance, all")

if __name__ == "__main__":
    asyncio.run(main())