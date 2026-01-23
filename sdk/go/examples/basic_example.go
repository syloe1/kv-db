package main

import (
	"context"
	"fmt"
	"log"
	"time"

	kvdb "github.com/kvdb/go-client"
)

func main() {
	// 配置客户端
	config := kvdb.DefaultClientConfig()
	config.ServerAddress = "localhost:50051"
	config.ConnectionTimeout = 5 * time.Second
	config.RequestTimeout = 10 * time.Second
	
	// 创建客户端
	client := kvdb.NewClient(config)
	defer client.Close()
	
	ctx := context.Background()
	
	// 连接到服务器
	if err := client.Connect(ctx); err != nil {
		log.Fatalf("Failed to connect: %v", err)
	}
	
	fmt.Println("Connected to KVDB server")
	
	// 基本操作示例
	if err := basicOperationsExample(ctx, client); err != nil {
		log.Printf("Basic operations error: %v", err)
	}
	
	// 批量操作示例
	if err := batchOperationsExample(ctx, client); err != nil {
		log.Printf("Batch operations error: %v", err)
	}
	
	// 扫描操作示例
	if err := scanOperationsExample(ctx, client); err != nil {
		log.Printf("Scan operations error: %v", err)
	}
	
	// 快照操作示例
	if err := snapshotOperationsExample(ctx, client); err != nil {
		log.Printf("Snapshot operations error: %v", err)
	}
	
	// 订阅示例
	if err := subscriptionExample(ctx, client); err != nil {
		log.Printf("Subscription error: %v", err)
	}
	
	// 管理操作示例
	if err := managementOperationsExample(ctx, client); err != nil {
		log.Printf("Management operations error: %v", err)
	}
}

func basicOperationsExample(ctx context.Context, client *kvdb.Client) error {
	fmt.Println("\n=== Basic Operations Example ===")
	
	// PUT操作
	if err := client.Put(ctx, "user:1001", "Alice"); err != nil {
		return fmt.Errorf("PUT failed: %w", err)
	}
	fmt.Println("PUT user:1001 = Alice")
	
	// GET操作
	value, found, err := client.Get(ctx, "user:1001")
	if err != nil {
		return fmt.Errorf("GET failed: %w", err)
	}
	if found {
		fmt.Printf("GET user:1001 = %s\n", value)
	} else {
		fmt.Println("GET user:1001 = <not found>")
	}
	
	// DELETE操作
	if err := client.Delete(ctx, "user:1001"); err != nil {
		return fmt.Errorf("DELETE failed: %w", err)
	}
	fmt.Println("DELETE user:1001")
	
	// 验证删除
	value, found, err = client.Get(ctx, "user:1001")
	if err != nil {
		return fmt.Errorf("GET after delete failed: %w", err)
	}
	if found {
		fmt.Printf("GET user:1001 after delete = %s\n", value)
	} else {
		fmt.Println("GET user:1001 after delete = <not found>")
	}
	
	return nil
}

func batchOperationsExample(ctx context.Context, client *kvdb.Client) error {
	fmt.Println("\n=== Batch Operations Example ===")
	
	// 批量PUT
	pairs := []kvdb.KeyValue{
		{Key: "user:1001", Value: "Alice"},
		{Key: "user:1002", Value: "Bob"},
		{Key: "user:1003", Value: "Charlie"},
		{Key: "user:1004", Value: "David"},
	}
	
	if err := client.BatchPut(ctx, pairs); err != nil {
		return fmt.Errorf("batch PUT failed: %w", err)
	}
	fmt.Printf("Batch PUT %d pairs\n", len(pairs))
	
	// 批量GET
	keys := []string{"user:1001", "user:1002", "user:1003"}
	results, err := client.BatchGet(ctx, keys)
	if err != nil {
		return fmt.Errorf("batch GET failed: %w", err)
	}
	
	fmt.Println("Batch GET results:")
	for _, kv := range results {
		fmt.Printf("  %s = %s\n", kv.Key, kv.Value)
	}
	
	return nil
}

func scanOperationsExample(ctx context.Context, client *kvdb.Client) error {
	fmt.Println("\n=== Scan Operations Example ===")
	
	// 前缀扫描
	results, err := client.PrefixScan(ctx, "user:", 10)
	if err != nil {
		return fmt.Errorf("prefix scan failed: %w", err)
	}
	
	fmt.Println("Prefix scan results:")
	for _, kv := range results {
		fmt.Printf("  %s = %s\n", kv.Key, kv.Value)
	}
	
	// 范围扫描
	scanOptions := &kvdb.ScanOptions{
		StartKey: "user:1000",
		EndKey:   "user:1999",
		Limit:    5,
	}
	
	results, err = client.Scan(ctx, scanOptions)
	if err != nil {
		return fmt.Errorf("range scan failed: %w", err)
	}
	
	fmt.Println("Range scan results:")
	for _, kv := range results {
		fmt.Printf("  %s = %s\n", kv.Key, kv.Value)
	}
	
	return nil
}

func snapshotOperationsExample(ctx context.Context, client *kvdb.Client) error {
	fmt.Println("\n=== Snapshot Operations Example ===")
	
	// 创建快照
	snapshot, err := client.CreateSnapshot(ctx)
	if err != nil {
		return fmt.Errorf("create snapshot failed: %w", err)
	}
	fmt.Printf("Created snapshot: %d\n", snapshot.ID)
	
	// 在快照上读取数据
	value, found, err := client.GetAtSnapshot(ctx, "user:1001", snapshot)
	if err != nil {
		return fmt.Errorf("get at snapshot failed: %w", err)
	}
	if found {
		fmt.Printf("Snapshot GET user:1001 = %s\n", value)
	} else {
		fmt.Println("Snapshot GET user:1001 = <not found>")
	}
	
	// 修改数据
	if err := client.Put(ctx, "user:1001", "Alice_Modified"); err != nil {
		return fmt.Errorf("PUT failed: %w", err)
	}
	
	// 当前读取
	currentValue, found, err := client.Get(ctx, "user:1001")
	if err != nil {
		return fmt.Errorf("current GET failed: %w", err)
	}
	if found {
		fmt.Printf("Current GET user:1001 = %s\n", currentValue)
	}
	
	// 快照读取（应该是旧值）
	snapshotValue, found, err := client.GetAtSnapshot(ctx, "user:1001", snapshot)
	if err != nil {
		return fmt.Errorf("snapshot GET failed: %w", err)
	}
	if found {
		fmt.Printf("Snapshot GET user:1001 = %s\n", snapshotValue)
	}
	
	// 释放快照
	if err := client.ReleaseSnapshot(ctx, snapshot); err != nil {
		return fmt.Errorf("release snapshot failed: %w", err)
	}
	fmt.Println("Released snapshot")
	
	return nil
}

func subscriptionExample(ctx context.Context, client *kvdb.Client) error {
	fmt.Println("\n=== Subscription Example ===")
	
	// 开始订阅
	subscription, err := client.Subscribe(ctx, "user:*", func(event *kvdb.SubscriptionEvent) {
		fmt.Printf("Subscription event: %s %s = %s\n", event.Operation, event.Key, event.Value)
	}, true)
	
	if err != nil {
		return fmt.Errorf("subscribe failed: %w", err)
	}
	
	fmt.Println("Started subscription")
	
	// 等待一段时间
	time.Sleep(1 * time.Second)
	
	// 触发一些事件
	client.Put(ctx, "user:1005", "Eve")
	client.Put(ctx, "user:1006", "Frank")
	client.Delete(ctx, "user:1004")
	
	// 等待事件处理
	time.Sleep(2 * time.Second)
	
	// 取消订阅
	subscription.Cancel()
	fmt.Println("Cancelled subscription")
	
	return nil
}

func managementOperationsExample(ctx context.Context, client *kvdb.Client) error {
	fmt.Println("\n=== Management Operations Example ===")
	
	// 获取统计信息
	stats, err := client.GetStats(ctx)
	if err != nil {
		return fmt.Errorf("get stats failed: %w", err)
	}
	
	fmt.Println("Database stats:")
	fmt.Printf("  Memtable size: %d\n", stats.MemtableSize)
	fmt.Printf("  WAL size: %d\n", stats.WALSize)
	fmt.Printf("  Cache hit rate: %.2f\n", stats.CacheHitRate)
	fmt.Printf("  Active snapshots: %d\n", stats.ActiveSnapshots)
	
	// 刷新数据
	if err := client.Flush(ctx); err != nil {
		return fmt.Errorf("flush failed: %w", err)
	}
	fmt.Println("Flushed data to disk")
	
	// 压缩数据
	if err := client.Compact(ctx); err != nil {
		return fmt.Errorf("compact failed: %w", err)
	}
	fmt.Println("Compacted data")
	
	return nil
}