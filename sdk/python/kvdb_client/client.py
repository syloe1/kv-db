"""
KVDB Python Client Implementation
"""

import grpc
import asyncio
import threading
from typing import List, Dict, Optional, Callable, Any, Union
from dataclasses import dataclass, field
from concurrent.futures import ThreadPoolExecutor
import json
import requests
import time

from .exceptions import KVDBError, ConnectionError, TimeoutError
from .types import KeyValue, ScanOptions, Snapshot
from . import kvdb_pb2
from . import kvdb_pb2_grpc


@dataclass
class ClientConfig:
    """客户端配置"""
    server_address: str = "localhost:50051"
    protocol: str = "grpc"  # grpc, http, tcp
    connection_timeout: float = 5.0
    request_timeout: float = 30.0
    max_retries: int = 3
    enable_compression: bool = True
    compression_type: str = "gzip"
    
    # 连接池配置
    max_connections: int = 10
    min_connections: int = 2
    connection_idle_timeout: float = 60.0
    
    # HTTP特定配置
    http_base_url: Optional[str] = None
    
    def __post_init__(self):
        if self.http_base_url is None:
            host, port = self.server_address.split(':')
            self.http_base_url = f"http://{host}:{int(port) + 1000}"  # HTTP端口通常是gRPC端口+1000


class KVDBClient:
    """KVDB Python客户端"""
    
    def __init__(self, config: ClientConfig):
        self.config = config
        self._connected = False
        self._channel = None
        self._stub = None
        self._executor = ThreadPoolExecutor(max_workers=10)
        self._subscriptions = {}
        self._subscription_counter = 0
        
        if config.protocol == "grpc":
            self._init_grpc()
        elif config.protocol == "http":
            self._init_http()
        else:
            raise ValueError(f"Unsupported protocol: {config.protocol}")
    
    def _init_grpc(self):
        """初始化gRPC连接"""
        options = [
            ('grpc.max_receive_message_length', 64 * 1024 * 1024),
            ('grpc.max_send_message_length', 64 * 1024 * 1024),
        ]
        
        if self.config.enable_compression:
            options.append(('grpc.default_compression_algorithm', grpc.Compression.Gzip))
        
        self._channel = grpc.insecure_channel(self.config.server_address, options=options)
        self._stub = kvdb_pb2_grpc.KVDBServiceStub(self._channel)
    
    def _init_http(self):
        """初始化HTTP连接"""
        self._session = requests.Session()
        self._session.timeout = self.config.request_timeout
    
    def connect(self) -> bool:
        """连接到服务器"""
        try:
            if self.config.protocol == "grpc":
                # 测试gRPC连接
                grpc.channel_ready_future(self._channel).result(timeout=self.config.connection_timeout)
            elif self.config.protocol == "http":
                # 测试HTTP连接
                response = self._session.get(f"{self.config.http_base_url}/health", 
                                           timeout=self.config.connection_timeout)
                response.raise_for_status()
            
            self._connected = True
            return True
        except Exception as e:
            raise ConnectionError(f"Failed to connect: {str(e)}")
    
    def disconnect(self):
        """断开连接"""
        self._connected = False
        
        # 取消所有订阅
        for subscription_id in list(self._subscriptions.keys()):
            self.cancel_subscription(subscription_id)
        
        if self.config.protocol == "grpc" and self._channel:
            self._channel.close()
        elif self.config.protocol == "http" and hasattr(self, '_session'):
            self._session.close()
    
    def is_connected(self) -> bool:
        """检查连接状态"""
        return self._connected
    
    def put(self, key: str, value: str) -> bool:
        """存储键值对"""
        if self.config.protocol == "grpc":
            return self._grpc_put(key, value)
        elif self.config.protocol == "http":
            return self._http_put(key, value)
    
    def _grpc_put(self, key: str, value: str) -> bool:
        """gRPC PUT操作"""
        request = kvdb_pb2.PutRequest(key=key, value=value)
        
        try:
            response = self._stub.Put(request, timeout=self.config.request_timeout)
            if not response.success:
                raise KVDBError(response.error_message)
            return True
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_put(self, key: str, value: str) -> bool:
        """HTTP PUT操作"""
        try:
            response = self._session.put(
                f"{self.config.http_base_url}/kv/{key}",
                json={"value": value}
            )
            response.raise_for_status()
            return True
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def get(self, key: str) -> Optional[str]:
        """获取键对应的值"""
        if self.config.protocol == "grpc":
            return self._grpc_get(key)
        elif self.config.protocol == "http":
            return self._http_get(key)
    
    def _grpc_get(self, key: str) -> Optional[str]:
        """gRPC GET操作"""
        request = kvdb_pb2.GetRequest(key=key)
        
        try:
            response = self._stub.Get(request, timeout=self.config.request_timeout)
            if not response.found:
                return None
            if response.error_message:
                raise KVDBError(response.error_message)
            return response.value
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_get(self, key: str) -> Optional[str]:
        """HTTP GET操作"""
        try:
            response = self._session.get(f"{self.config.http_base_url}/kv/{key}")
            if response.status_code == 404:
                return None
            response.raise_for_status()
            return response.json()["value"]
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def delete(self, key: str) -> bool:
        """删除键"""
        if self.config.protocol == "grpc":
            return self._grpc_delete(key)
        elif self.config.protocol == "http":
            return self._http_delete(key)
    
    def _grpc_delete(self, key: str) -> bool:
        """gRPC DELETE操作"""
        request = kvdb_pb2.DeleteRequest(key=key)
        
        try:
            response = self._stub.Delete(request, timeout=self.config.request_timeout)
            if not response.success:
                raise KVDBError(response.error_message)
            return True
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_delete(self, key: str) -> bool:
        """HTTP DELETE操作"""
        try:
            response = self._session.delete(f"{self.config.http_base_url}/kv/{key}")
            response.raise_for_status()
            return True
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    async def put_async(self, key: str, value: str) -> bool:
        """异步存储键值对"""
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(self._executor, self.put, key, value)
    
    async def get_async(self, key: str) -> Optional[str]:
        """异步获取键对应的值"""
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(self._executor, self.get, key)
    
    async def delete_async(self, key: str) -> bool:
        """异步删除键"""
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(self._executor, self.delete, key)
    
    def batch_put(self, pairs: List[KeyValue]) -> bool:
        """批量存储"""
        if self.config.protocol == "grpc":
            return self._grpc_batch_put(pairs)
        elif self.config.protocol == "http":
            return self._http_batch_put(pairs)
    
    def _grpc_batch_put(self, pairs: List[KeyValue]) -> bool:
        """gRPC批量PUT操作"""
        request = kvdb_pb2.BatchPutRequest()
        
        for pair in pairs:
            kv = request.pairs.add()
            kv.key = pair.key
            kv.value = pair.value
        
        try:
            response = self._stub.BatchPut(request, timeout=self.config.request_timeout)
            if not response.success:
                raise KVDBError(response.error_message)
            return True
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_batch_put(self, pairs: List[KeyValue]) -> bool:
        """HTTP批量PUT操作"""
        try:
            data = [{"key": pair.key, "value": pair.value} for pair in pairs]
            response = self._session.post(
                f"{self.config.http_base_url}/batch/put",
                json={"pairs": data}
            )
            response.raise_for_status()
            return True
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def batch_get(self, keys: List[str]) -> List[KeyValue]:
        """批量获取"""
        if self.config.protocol == "grpc":
            return self._grpc_batch_get(keys)
        elif self.config.protocol == "http":
            return self._http_batch_get(keys)
    
    def _grpc_batch_get(self, keys: List[str]) -> List[KeyValue]:
        """gRPC批量GET操作"""
        request = kvdb_pb2.BatchGetRequest(keys=keys)
        
        try:
            response = self._stub.BatchGet(request, timeout=self.config.request_timeout)
            if response.error_message:
                raise KVDBError(response.error_message)
            
            result = []
            for pair in response.pairs:
                result.append(KeyValue(key=pair.key, value=pair.value))
            return result
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_batch_get(self, keys: List[str]) -> List[KeyValue]:
        """HTTP批量GET操作"""
        try:
            response = self._session.post(
                f"{self.config.http_base_url}/batch/get",
                json={"keys": keys}
            )
            response.raise_for_status()
            
            result = []
            for pair in response.json()["pairs"]:
                result.append(KeyValue(key=pair["key"], value=pair["value"]))
            return result
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def scan(self, options: ScanOptions) -> List[KeyValue]:
        """范围扫描"""
        if self.config.protocol == "grpc":
            return self._grpc_scan(options)
        elif self.config.protocol == "http":
            return self._http_scan(options)
    
    def _grpc_scan(self, options: ScanOptions) -> List[KeyValue]:
        """gRPC扫描操作"""
        request = kvdb_pb2.ScanRequest(
            start_key=options.start_key,
            end_key=options.end_key,
            limit=options.limit
        )
        
        try:
            result = []
            for response in self._stub.Scan(request, timeout=self.config.request_timeout):
                result.append(KeyValue(key=response.key, value=response.value))
            return result
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_scan(self, options: ScanOptions) -> List[KeyValue]:
        """HTTP扫描操作"""
        try:
            params = {
                "start_key": options.start_key,
                "end_key": options.end_key,
                "limit": options.limit
            }
            response = self._session.get(f"{self.config.http_base_url}/scan", params=params)
            response.raise_for_status()
            
            result = []
            for pair in response.json()["pairs"]:
                result.append(KeyValue(key=pair["key"], value=pair["value"]))
            return result
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def prefix_scan(self, prefix: str, limit: int = 1000) -> List[KeyValue]:
        """前缀扫描"""
        if self.config.protocol == "grpc":
            return self._grpc_prefix_scan(prefix, limit)
        elif self.config.protocol == "http":
            return self._http_prefix_scan(prefix, limit)
    
    def _grpc_prefix_scan(self, prefix: str, limit: int) -> List[KeyValue]:
        """gRPC前缀扫描"""
        request = kvdb_pb2.PrefixScanRequest(prefix=prefix, limit=limit)
        
        try:
            result = []
            for response in self._stub.PrefixScan(request, timeout=self.config.request_timeout):
                result.append(KeyValue(key=response.key, value=response.value))
            return result
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_prefix_scan(self, prefix: str, limit: int) -> List[KeyValue]:
        """HTTP前缀扫描"""
        try:
            params = {"prefix": prefix, "limit": limit}
            response = self._session.get(f"{self.config.http_base_url}/prefix_scan", params=params)
            response.raise_for_status()
            
            result = []
            for pair in response.json()["pairs"]:
                result.append(KeyValue(key=pair["key"], value=pair["value"]))
            return result
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def create_snapshot(self) -> Snapshot:
        """创建快照"""
        if self.config.protocol == "grpc":
            return self._grpc_create_snapshot()
        elif self.config.protocol == "http":
            return self._http_create_snapshot()
    
    def _grpc_create_snapshot(self) -> Snapshot:
        """gRPC创建快照"""
        request = kvdb_pb2.CreateSnapshotRequest()
        
        try:
            response = self._stub.CreateSnapshot(request, timeout=self.config.request_timeout)
            if response.error_message:
                raise KVDBError(response.error_message)
            return Snapshot(snapshot_id=response.snapshot_id)
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_create_snapshot(self) -> Snapshot:
        """HTTP创建快照"""
        try:
            response = self._session.post(f"{self.config.http_base_url}/snapshot")
            response.raise_for_status()
            return Snapshot(snapshot_id=response.json()["snapshot_id"])
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def release_snapshot(self, snapshot: Snapshot) -> bool:
        """释放快照"""
        if self.config.protocol == "grpc":
            return self._grpc_release_snapshot(snapshot)
        elif self.config.protocol == "http":
            return self._http_release_snapshot(snapshot)
    
    def _grpc_release_snapshot(self, snapshot: Snapshot) -> bool:
        """gRPC释放快照"""
        request = kvdb_pb2.ReleaseSnapshotRequest(snapshot_id=snapshot.snapshot_id)
        
        try:
            response = self._stub.ReleaseSnapshot(request, timeout=self.config.request_timeout)
            if not response.success:
                raise KVDBError(response.error_message)
            return True
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_release_snapshot(self, snapshot: Snapshot) -> bool:
        """HTTP释放快照"""
        try:
            response = self._session.delete(f"{self.config.http_base_url}/snapshot/{snapshot.snapshot_id}")
            response.raise_for_status()
            return True
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def get_at_snapshot(self, key: str, snapshot: Snapshot) -> Optional[str]:
        """在快照上读取数据"""
        if self.config.protocol == "grpc":
            return self._grpc_get_at_snapshot(key, snapshot)
        elif self.config.protocol == "http":
            return self._http_get_at_snapshot(key, snapshot)
    
    def _grpc_get_at_snapshot(self, key: str, snapshot: Snapshot) -> Optional[str]:
        """gRPC快照读取"""
        request = kvdb_pb2.GetAtSnapshotRequest(key=key, snapshot_id=snapshot.snapshot_id)
        
        try:
            response = self._stub.GetAtSnapshot(request, timeout=self.config.request_timeout)
            if not response.found:
                return None
            if response.error_message:
                raise KVDBError(response.error_message)
            return response.value
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_get_at_snapshot(self, key: str, snapshot: Snapshot) -> Optional[str]:
        """HTTP快照读取"""
        try:
            response = self._session.get(
                f"{self.config.http_base_url}/snapshot/{snapshot.snapshot_id}/kv/{key}"
            )
            if response.status_code == 404:
                return None
            response.raise_for_status()
            return response.json()["value"]
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def flush(self) -> bool:
        """刷新数据到磁盘"""
        if self.config.protocol == "grpc":
            return self._grpc_flush()
        elif self.config.protocol == "http":
            return self._http_flush()
    
    def _grpc_flush(self) -> bool:
        """gRPC刷新操作"""
        request = kvdb_pb2.FlushRequest()
        
        try:
            response = self._stub.Flush(request, timeout=self.config.request_timeout)
            if not response.success:
                raise KVDBError(response.error_message)
            return True
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_flush(self) -> bool:
        """HTTP刷新操作"""
        try:
            response = self._session.post(f"{self.config.http_base_url}/admin/flush")
            response.raise_for_status()
            return True
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def compact(self) -> bool:
        """压缩数据"""
        if self.config.protocol == "grpc":
            return self._grpc_compact()
        elif self.config.protocol == "http":
            return self._http_compact()
    
    def _grpc_compact(self) -> bool:
        """gRPC压缩操作"""
        request = kvdb_pb2.CompactRequest()
        
        try:
            response = self._stub.Compact(request, timeout=self.config.request_timeout)
            if not response.success:
                raise KVDBError(response.error_message)
            return True
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_compact(self) -> bool:
        """HTTP压缩操作"""
        try:
            response = self._session.post(f"{self.config.http_base_url}/admin/compact")
            response.raise_for_status()
            return True
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def get_stats(self) -> Dict[str, Any]:
        """获取数据库统计信息"""
        if self.config.protocol == "grpc":
            return self._grpc_get_stats()
        elif self.config.protocol == "http":
            return self._http_get_stats()
    
    def _grpc_get_stats(self) -> Dict[str, Any]:
        """gRPC获取统计信息"""
        request = kvdb_pb2.GetStatsRequest()
        
        try:
            response = self._stub.GetStats(request, timeout=self.config.request_timeout)
            return {
                "memtable_size": response.memtable_size,
                "wal_size": response.wal_size,
                "cache_hit_rate": response.cache_hit_rate,
                "active_snapshots": response.active_snapshots
            }
        except grpc.RpcError as e:
            raise KVDBError(f"gRPC error: {e.details()}")
    
    def _http_get_stats(self) -> Dict[str, Any]:
        """HTTP获取统计信息"""
        try:
            response = self._session.get(f"{self.config.http_base_url}/admin/stats")
            response.raise_for_status()
            return response.json()
        except requests.RequestException as e:
            raise KVDBError(f"HTTP error: {str(e)}")
    
    def subscribe(self, pattern: str, callback: Callable[[str, str, str], None], 
                 include_deletes: bool = False) -> int:
        """订阅键变化事件"""
        if self.config.protocol != "grpc":
            raise KVDBError("Subscription is only supported with gRPC protocol")
        
        subscription_id = self._subscription_counter
        self._subscription_counter += 1
        
        def subscription_thread():
            request = kvdb_pb2.SubscribeRequest(
                key_pattern=pattern,
                include_deletes=include_deletes
            )
            
            try:
                for response in self._stub.Subscribe(request):
                    if subscription_id not in self._subscriptions:
                        break
                    callback(response.key, response.value, response.operation)
            except grpc.RpcError as e:
                if subscription_id in self._subscriptions:
                    print(f"Subscription error: {e.details()}")
            finally:
                if subscription_id in self._subscriptions:
                    del self._subscriptions[subscription_id]
        
        thread = threading.Thread(target=subscription_thread, daemon=True)
        self._subscriptions[subscription_id] = thread
        thread.start()
        
        return subscription_id
    
    def cancel_subscription(self, subscription_id: int):
        """取消订阅"""
        if subscription_id in self._subscriptions:
            del self._subscriptions[subscription_id]
    
    def __enter__(self):
        """上下文管理器入口"""
        self.connect()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """上下文管理器出口"""
        self.disconnect()