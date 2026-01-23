package com.kvdb.client;

import com.kvdb.proto.KVDBServiceGrpc;
import com.kvdb.proto.Kvdb.*;
import io.grpc.ManagedChannel;
import io.grpc.ManagedChannelBuilder;
import io.grpc.stub.StreamObserver;

import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;
import java.util.stream.Collectors;

/**
 * KVDB Java客户端
 */
public class KVDBClient implements AutoCloseable {
    
    private final ClientConfig config;
    private ManagedChannel channel;
    private KVDBServiceGrpc.KVDBServiceBlockingStub blockingStub;
    private KVDBServiceGrpc.KVDBServiceStub asyncStub;
    private final Map<Integer, StreamObserver<SubscribeRequest>> subscriptions = new ConcurrentHashMap<>();
    private final AtomicInteger subscriptionCounter = new AtomicInteger(0);
    
    public KVDBClient(ClientConfig config) {
        this.config = config;
        initializeChannel();
    }
    
    private void initializeChannel() {
        ManagedChannelBuilder<?> channelBuilder = ManagedChannelBuilder
            .forTarget(config.getServerAddress())
            .usePlaintext();
        
        if (config.getMaxInboundMessageSize() > 0) {
            channelBuilder.maxInboundMessageSize(config.getMaxInboundMessageSize());
        }
        
        if (config.getKeepAliveTime() > 0) {
            channelBuilder.keepAliveTime(config.getKeepAliveTime(), TimeUnit.MILLISECONDS);
        }
        
        this.channel = channelBuilder.build();
        this.blockingStub = KVDBServiceGrpc.newBlockingStub(channel);
        this.asyncStub = KVDBServiceGrpc.newStub(channel);
        
        if (config.getRequestTimeout() > 0) {
            this.blockingStub = blockingStub.withDeadlineAfter(config.getRequestTimeout(), TimeUnit.MILLISECONDS);
        }
    }
    
    /**
     * 连接到服务器
     */
    public boolean connect() throws KVDBException {
        try {
            // 测试连接
            GetStatsRequest request = GetStatsRequest.newBuilder().build();
            blockingStub.getStats(request);
            return true;
        } catch (Exception e) {
            throw new KVDBException("Failed to connect to server", e);
        }
    }
    
    /**
     * 检查连接状态
     */
    public boolean isConnected() {
        return !channel.isShutdown() && !channel.isTerminated();
    }
    
    /**
     * 存储键值对
     */
    public boolean put(String key, String value) throws KVDBException {
        try {
            PutRequest request = PutRequest.newBuilder()
                .setKey(key)
                .setValue(value)
                .build();
            
            PutResponse response = blockingStub.put(request);
            
            if (!response.getSuccess()) {
                throw new KVDBException(response.getErrorMessage());
            }
            
            return true;
        } catch (Exception e) {
            throw new KVDBException("PUT operation failed", e);
        }
    }
    
    /**
     * 获取键对应的值
     */
    public String get(String key) throws KVDBException {
        try {
            GetRequest request = GetRequest.newBuilder()
                .setKey(key)
                .build();
            
            GetResponse response = blockingStub.get(request);
            
            if (!response.getErrorMessage().isEmpty()) {
                throw new KVDBException(response.getErrorMessage());
            }
            
            return response.getFound() ? response.getValue() : null;
        } catch (Exception e) {
            throw new KVDBException("GET operation failed", e);
        }
    }
    
    /**
     * 删除键
     */
    public boolean delete(String key) throws KVDBException {
        try {
            DeleteRequest request = DeleteRequest.newBuilder()
                .setKey(key)
                .build();
            
            DeleteResponse response = blockingStub.delete(request);
            
            if (!response.getSuccess()) {
                throw new KVDBException(response.getErrorMessage());
            }
            
            return true;
        } catch (Exception e) {
            throw new KVDBException("DELETE operation failed", e);
        }
    }
    
    /**
     * 异步存储键值对
     */
    public CompletableFuture<Boolean> putAsync(String key, String value) {
        CompletableFuture<Boolean> future = new CompletableFuture<>();
        
        PutRequest request = PutRequest.newBuilder()
            .setKey(key)
            .setValue(value)
            .build();
        
        asyncStub.put(request, new StreamObserver<PutResponse>() {
            @Override
            public void onNext(PutResponse response) {
                if (response.getSuccess()) {
                    future.complete(true);
                } else {
                    future.completeExceptionally(new KVDBException(response.getErrorMessage()));
                }
            }
            
            @Override
            public void onError(Throwable t) {
                future.completeExceptionally(new KVDBException("Async PUT failed", t));
            }
            
            @Override
            public void onCompleted() {
                // Nothing to do
            }
        });
        
        return future;
    }
    
    /**
     * 异步获取键对应的值
     */
    public CompletableFuture<String> getAsync(String key) {
        CompletableFuture<String> future = new CompletableFuture<>();
        
        GetRequest request = GetRequest.newBuilder()
            .setKey(key)
            .build();
        
        asyncStub.get(request, new StreamObserver<GetResponse>() {
            @Override
            public void onNext(GetResponse response) {
                if (!response.getErrorMessage().isEmpty()) {
                    future.completeExceptionally(new KVDBException(response.getErrorMessage()));
                } else {
                    future.complete(response.getFound() ? response.getValue() : null);
                }
            }
            
            @Override
            public void onError(Throwable t) {
                future.completeExceptionally(new KVDBException("Async GET failed", t));
            }
            
            @Override
            public void onCompleted() {
                // Nothing to do
            }
        });
        
        return future;
    }
    
    /**
     * 异步删除键
     */
    public CompletableFuture<Boolean> deleteAsync(String key) {
        CompletableFuture<Boolean> future = new CompletableFuture<>();
        
        DeleteRequest request = DeleteRequest.newBuilder()
            .setKey(key)
            .build();
        
        asyncStub.delete(request, new StreamObserver<DeleteResponse>() {
            @Override
            public void onNext(DeleteResponse response) {
                if (response.getSuccess()) {
                    future.complete(true);
                } else {
                    future.completeExceptionally(new KVDBException(response.getErrorMessage()));
                }
            }
            
            @Override
            public void onError(Throwable t) {
                future.completeExceptionally(new KVDBException("Async DELETE failed", t));
            }
            
            @Override
            public void onCompleted() {
                // Nothing to do
            }
        });
        
        return future;
    }
    
    /**
     * 批量存储
     */
    public boolean batchPut(List<KeyValue> pairs) throws KVDBException {
        try {
            BatchPutRequest.Builder requestBuilder = BatchPutRequest.newBuilder();
            
            for (KeyValue pair : pairs) {
                com.kvdb.proto.Kvdb.KeyValue kv = com.kvdb.proto.Kvdb.KeyValue.newBuilder()
                    .setKey(pair.getKey())
                    .setValue(pair.getValue())
                    .build();
                requestBuilder.addPairs(kv);
            }
            
            BatchPutResponse response = blockingStub.batchPut(requestBuilder.build());
            
            if (!response.getSuccess()) {
                throw new KVDBException(response.getErrorMessage());
            }
            
            return true;
        } catch (Exception e) {
            throw new KVDBException("Batch PUT operation failed", e);
        }
    }
    
    /**
     * 批量获取
     */
    public List<KeyValue> batchGet(List<String> keys) throws KVDBException {
        try {
            BatchGetRequest request = BatchGetRequest.newBuilder()
                .addAllKeys(keys)
                .build();
            
            BatchGetResponse response = blockingStub.batchGet(request);
            
            if (!response.getErrorMessage().isEmpty()) {
                throw new KVDBException(response.getErrorMessage());
            }
            
            return response.getPairsList().stream()
                .map(kv -> new KeyValue(kv.getKey(), kv.getValue()))
                .collect(Collectors.toList());
        } catch (Exception e) {
            throw new KVDBException("Batch GET operation failed", e);
        }
    }
    
    /**
     * 范围扫描
     */
    public List<KeyValue> scan(ScanOptions options) throws KVDBException {
        try {
            ScanRequest request = ScanRequest.newBuilder()
                .setStartKey(options.getStartKey())
                .setEndKey(options.getEndKey())
                .setLimit(options.getLimit())
                .build();
            
            return blockingStub.scan(request)
                .forEachRemaining(response -> new KeyValue(response.getKey(), response.getValue()))
                .collect(Collectors.toList());
        } catch (Exception e) {
            throw new KVDBException("Scan operation failed", e);
        }
    }
    
    /**
     * 前缀扫描
     */
    public List<KeyValue> prefixScan(String prefix, int limit) throws KVDBException {
        try {
            PrefixScanRequest request = PrefixScanRequest.newBuilder()
                .setPrefix(prefix)
                .setLimit(limit)
                .build();
            
            return blockingStub.prefixScan(request)
                .forEachRemaining(response -> new KeyValue(response.getKey(), response.getValue()))
                .collect(Collectors.toList());
        } catch (Exception e) {
            throw new KVDBException("Prefix scan operation failed", e);
        }
    }
    
    /**
     * 创建快照
     */
    public Snapshot createSnapshot() throws KVDBException {
        try {
            CreateSnapshotRequest request = CreateSnapshotRequest.newBuilder().build();
            CreateSnapshotResponse response = blockingStub.createSnapshot(request);
            
            if (!response.getErrorMessage().isEmpty()) {
                throw new KVDBException(response.getErrorMessage());
            }
            
            return new Snapshot(response.getSnapshotId());
        } catch (Exception e) {
            throw new KVDBException("Create snapshot operation failed", e);
        }
    }
    
    /**
     * 释放快照
     */
    public boolean releaseSnapshot(Snapshot snapshot) throws KVDBException {
        try {
            ReleaseSnapshotRequest request = ReleaseSnapshotRequest.newBuilder()
                .setSnapshotId(snapshot.getSnapshotId())
                .build();
            
            ReleaseSnapshotResponse response = blockingStub.releaseSnapshot(request);
            
            if (!response.getSuccess()) {
                throw new KVDBException(response.getErrorMessage());
            }
            
            return true;
        } catch (Exception e) {
            throw new KVDBException("Release snapshot operation failed", e);
        }
    }
    
    /**
     * 在快照上读取数据
     */
    public String getAtSnapshot(String key, Snapshot snapshot) throws KVDBException {
        try {
            GetAtSnapshotRequest request = GetAtSnapshotRequest.newBuilder()
                .setKey(key)
                .setSnapshotId(snapshot.getSnapshotId())
                .build();
            
            GetAtSnapshotResponse response = blockingStub.getAtSnapshot(request);
            
            if (!response.getErrorMessage().isEmpty()) {
                throw new KVDBException(response.getErrorMessage());
            }
            
            return response.getFound() ? response.getValue() : null;
        } catch (Exception e) {
            throw new KVDBException("Get at snapshot operation failed", e);
        }
    }
    
    /**
     * 刷新数据到磁盘
     */
    public boolean flush() throws KVDBException {
        try {
            FlushRequest request = FlushRequest.newBuilder().build();
            FlushResponse response = blockingStub.flush(request);
            
            if (!response.getSuccess()) {
                throw new KVDBException(response.getErrorMessage());
            }
            
            return true;
        } catch (Exception e) {
            throw new KVDBException("Flush operation failed", e);
        }
    }
    
    /**
     * 压缩数据
     */
    public boolean compact() throws KVDBException {
        try {
            CompactRequest request = CompactRequest.newBuilder().build();
            CompactResponse response = blockingStub.compact(request);
            
            if (!response.getSuccess()) {
                throw new KVDBException(response.getErrorMessage());
            }
            
            return true;
        } catch (Exception e) {
            throw new KVDBException("Compact operation failed", e);
        }
    }
    
    /**
     * 获取数据库统计信息
     */
    public DatabaseStats getStats() throws KVDBException {
        try {
            GetStatsRequest request = GetStatsRequest.newBuilder().build();
            GetStatsResponse response = blockingStub.getStats(request);
            
            return new DatabaseStats(
                response.getMemtableSize(),
                response.getWalSize(),
                response.getCacheHitRate(),
                response.getActiveSnapshots()
            );
        } catch (Exception e) {
            throw new KVDBException("Get stats operation failed", e);
        }
    }
    
    /**
     * 订阅键变化事件
     */
    public int subscribe(String pattern, Consumer<SubscriptionEvent> callback, boolean includeDeletes) throws KVDBException {
        int subscriptionId = subscriptionCounter.incrementAndGet();
        
        SubscribeRequest request = SubscribeRequest.newBuilder()
            .setKeyPattern(pattern)
            .setIncludeDeletes(includeDeletes)
            .build();
        
        StreamObserver<SubscribeResponse> responseObserver = new StreamObserver<SubscribeResponse>() {
            @Override
            public void onNext(SubscribeResponse response) {
                SubscriptionEvent event = new SubscriptionEvent(
                    response.getKey(),
                    response.getValue(),
                    response.getOperation(),
                    response.getTimestamp()
                );
                callback.accept(event);
            }
            
            @Override
            public void onError(Throwable t) {
                subscriptions.remove(subscriptionId);
                // 可以添加错误处理逻辑
            }
            
            @Override
            public void onCompleted() {
                subscriptions.remove(subscriptionId);
            }
        };
        
        StreamObserver<SubscribeRequest> requestObserver = asyncStub.subscribe(responseObserver);
        requestObserver.onNext(request);
        
        subscriptions.put(subscriptionId, requestObserver);
        
        return subscriptionId;
    }
    
    /**
     * 取消订阅
     */
    public void cancelSubscription(int subscriptionId) {
        StreamObserver<SubscribeRequest> observer = subscriptions.remove(subscriptionId);
        if (observer != null) {
            observer.onCompleted();
        }
    }
    
    /**
     * 关闭客户端
     */
    @Override
    public void close() {
        // 取消所有订阅
        subscriptions.keySet().forEach(this::cancelSubscription);
        
        // 关闭通道
        if (channel != null && !channel.isShutdown()) {
            try {
                channel.shutdown().awaitTermination(5, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                channel.shutdownNow();
            }
        }
    }
}