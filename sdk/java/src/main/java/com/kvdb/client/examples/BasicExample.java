package com.kvdb.client.examples;

import com.kvdb.client.*;
import com.kvdb.client.types.*;

import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

/**
 * KVDB Java客户端基础示例
 */
public class BasicExample {
    
    public static void main(String[] args) {
        // 配置客户端
        ClientConfig config = new ClientConfig()
            .setServerAddress("localhost:50051")
            .setProtocol("grpc")
            .setConnectionTimeout(5000)
            .setRequestTimeout(10000);
        
        // 使用try-with-resources自动关闭客户端
        try (KVDBClient client = new KVDBClient(config)) {
            
            // 连接到服务器
            if (client.connect()) {
                System.out.println("Connected to KVDB server");
            } else {
                System.err.println("Failed to connect to server");
                return;
            }
            
            // 基本操作示例
            basicOperationsExample(client);
            
            // 批量操作示例
            batchOperationsExample(client);
            
            // 扫描操作示例
            scanOperationsExample(client);
            
            // 快照操作示例
            snapshotOperationsExample(client);
            
            // 异步操作示例
            asyncOperationsExample(client);
            
            // 订阅示例
            subscriptionExample(client);
            
            // 管理操作示例
            managementOperationsExample(client);
            
        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    private static void basicOperationsExample(KVDBClient client) throws KVDBException {
        System.out.println("\n=== Basic Operations Example ===");
        
        // PUT操作
        client.put("user:1001", "Alice");
        System.out.println("PUT user:1001 = Alice");
        
        // GET操作
        String value = client.get("user:1001");
        System.out.println("GET user:1001 = " + value);
        
        // DELETE操作
        client.delete("user:1001");
        System.out.println("DELETE user:1001");
        
        // 验证删除
        value = client.get("user:1001");
        System.out.println("GET user:1001 after delete = " + value);
    }
    
    private static void batchOperationsExample(KVDBClient client) throws KVDBException {
        System.out.println("\n=== Batch Operations Example ===");
        
        // 批量PUT
        List<KeyValue> pairs = Arrays.asList(
            new KeyValue("user:1001", "Alice"),
            new KeyValue("user:1002", "Bob"),
            new KeyValue("user:1003", "Charlie"),
            new KeyValue("user:1004", "David")
        );
        
        client.batchPut(pairs);
        System.out.println("Batch PUT " + pairs.size() + " pairs");
        
        // 批量GET
        List<String> keys = Arrays.asList("user:1001", "user:1002", "user:1003");
        List<KeyValue> results = client.batchGet(keys);
        
        System.out.println("Batch GET results:");
        for (KeyValue kv : results) {
            System.out.println("  " + kv.getKey() + " = " + kv.getValue());
        }
    }
    
    private static void scanOperationsExample(KVDBClient client) throws KVDBException {
        System.out.println("\n=== Scan Operations Example ===");
        
        // 前缀扫描
        List<KeyValue> results = client.prefixScan("user:", 10);
        System.out.println("Prefix scan results:");
        for (KeyValue kv : results) {
            System.out.println("  " + kv.getKey() + " = " + kv.getValue());
        }
        
        // 范围扫描
        ScanOptions scanOptions = new ScanOptions()
            .setStartKey("user:1000")
            .setEndKey("user:1999")
            .setLimit(5);
        
        results = client.scan(scanOptions);
        System.out.println("Range scan results:");
        for (KeyValue kv : results) {
            System.out.println("  " + kv.getKey() + " = " + kv.getValue());
        }
    }
    
    private static void snapshotOperationsExample(KVDBClient client) throws KVDBException {
        System.out.println("\n=== Snapshot Operations Example ===");
        
        // 创建快照
        Snapshot snapshot = client.createSnapshot();
        System.out.println("Created snapshot: " + snapshot.getSnapshotId());
        
        // 在快照上读取数据
        String value = client.getAtSnapshot("user:1001", snapshot);
        System.out.println("Snapshot GET user:1001 = " + value);
        
        // 修改数据
        client.put("user:1001", "Alice_Modified");
        
        // 当前读取
        String currentValue = client.get("user:1001");
        System.out.println("Current GET user:1001 = " + currentValue);
        
        // 快照读取（应该是旧值）
        String snapshotValue = client.getAtSnapshot("user:1001", snapshot);
        System.out.println("Snapshot GET user:1001 = " + snapshotValue);
        
        // 释放快照
        client.releaseSnapshot(snapshot);
        System.out.println("Released snapshot");
    }
    
    private static void asyncOperationsExample(KVDBClient client) throws Exception {
        System.out.println("\n=== Async Operations Example ===");
        
        // 异步PUT操作
        CompletableFuture<Boolean>[] putFutures = new CompletableFuture[5];
        for (int i = 0; i < 5; i++) {
            putFutures[i] = client.putAsync("async:key" + i, "value" + i);
        }
        
        // 等待所有PUT操作完成
        CompletableFuture.allOf(putFutures).get(10, TimeUnit.SECONDS);
        System.out.println("Completed 5 async PUT operations");
        
        // 异步GET操作
        CompletableFuture<String>[] getFutures = new CompletableFuture[5];
        for (int i = 0; i < 5; i++) {
            getFutures[i] = client.getAsync("async:key" + i);
        }
        
        // 等待所有GET操作完成
        CompletableFuture.allOf(getFutures).get(10, TimeUnit.SECONDS);
        System.out.println("Async GET results:");
        for (int i = 0; i < 5; i++) {
            String value = getFutures[i].get();
            System.out.println("  async:key" + i + " = " + value);
        }
    }
    
    private static void subscriptionExample(KVDBClient client) throws Exception {
        System.out.println("\n=== Subscription Example ===");
        
        // 开始订阅
        int subscriptionId = client.subscribe("user:*", event -> {
            System.out.println("Subscription event: " + event.getOperation() + 
                             " " + event.getKey() + " = " + event.getValue());
        }, true);
        
        System.out.println("Started subscription " + subscriptionId);
        
        // 等待一段时间
        Thread.sleep(1000);
        
        // 触发一些事件
        client.put("user:1005", "Eve");
        client.put("user:1006", "Frank");
        client.delete("user:1004");
        
        // 等待事件处理
        Thread.sleep(2000);
        
        // 取消订阅
        client.cancelSubscription(subscriptionId);
        System.out.println("Cancelled subscription");
    }
    
    private static void managementOperationsExample(KVDBClient client) throws KVDBException {
        System.out.println("\n=== Management Operations Example ===");
        
        // 获取统计信息
        DatabaseStats stats = client.getStats();
        System.out.println("Database stats:");
        System.out.println("  Memtable size: " + stats.getMemtableSize());
        System.out.println("  WAL size: " + stats.getWalSize());
        System.out.println("  Cache hit rate: " + stats.getCacheHitRate());
        System.out.println("  Active snapshots: " + stats.getActiveSnapshots());
        
        // 刷新数据
        client.flush();
        System.out.println("Flushed data to disk");
        
        // 压缩数据
        client.compact();
        System.out.println("Compacted data");
    }
}