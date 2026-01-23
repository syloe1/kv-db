package kvdb

import (
	"context"
	"fmt"
	"io"
	"sync"
	"sync/atomic"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	pb "github.com/kvdb/proto"
)

// ClientConfig 客户端配置
type ClientConfig struct {
	ServerAddress      string        `json:"server_address"`
	Protocol          string        `json:"protocol"`
	ConnectionTimeout time.Duration `json:"connection_timeout"`
	RequestTimeout    time.Duration `json:"request_timeout"`
	MaxRetries        int           `json:"max_retries"`
	EnableCompression bool          `json:"enable_compression"`
	CompressionType   string        `json:"compression_type"`
	
	// 连接池配置
	MaxConnections        int           `json:"max_connections"`
	MinConnections        int           `json:"min_connections"`
	ConnectionIdleTimeout time.Duration `json:"connection_idle_timeout"`
	
	// gRPC特定配置
	MaxRecvMsgSize int `json:"max_recv_msg_size"`
	MaxSendMsgSize int `json:"max_send_msg_size"`
	
	// HTTP特定配置
	HTTPBaseURL string `json:"http_base_url"`
}

// DefaultClientConfig 返回默认客户端配置
func DefaultClientConfig() *ClientConfig {
	return &ClientConfig{
		ServerAddress:         "localhost:50051",
		Protocol:             "grpc",
		ConnectionTimeout:    5 * time.Second,
		RequestTimeout:       30 * time.Second,
		MaxRetries:           3,
		EnableCompression:    true,
		CompressionType:      "gzip",
		MaxConnections:       10,
		MinConnections:       2,
		ConnectionIdleTimeout: 60 * time.Second,
		MaxRecvMsgSize:       64 * 1024 * 1024, // 64MB
		MaxSendMsgSize:       64 * 1024 * 1024, // 64MB
		HTTPBaseURL:          "http://localhost:8080",
	}
}

// KeyValue 键值对
type KeyValue struct {
	Key   string `json:"key"`
	Value string `json:"value"`
}

// ScanOptions 扫描选项
type ScanOptions struct {
	StartKey string `json:"start_key"`
	EndKey   string `json:"end_key"`
	Prefix   string `json:"prefix"`
	Limit    int32  `json:"limit"`
	Reverse  bool   `json:"reverse"`
}

// Snapshot 快照
type Snapshot struct {
	ID uint64 `json:"id"`
}

// DatabaseStats 数据库统计信息
type DatabaseStats struct {
	MemtableSize     uint64  `json:"memtable_size"`
	WALSize          uint64  `json:"wal_size"`
	CacheHitRate     float64 `json:"cache_hit_rate"`
	ActiveSnapshots  int32   `json:"active_snapshots"`
}

// SubscriptionEvent 订阅事件
type SubscriptionEvent struct {
	Key       string `json:"key"`
	Value     string `json:"value"`
	Operation string `json:"operation"`
	Timestamp uint64 `json:"timestamp"`
}

// SubscriptionCallback 订阅回调函数
type SubscriptionCallback func(event *SubscriptionEvent)

// Subscription 订阅句柄
type Subscription struct {
	id       int32
	client   *Client
	cancel   context.CancelFunc
	active   int32
}

// Cancel 取消订阅
func (s *Subscription) Cancel() {
	if atomic.CompareAndSwapInt32(&s.active, 1, 0) {
		s.cancel()
		s.client.removeSubscription(s.id)
	}
}

// IsActive 检查订阅是否活跃
func (s *Subscription) IsActive() bool {
	return atomic.LoadInt32(&s.active) == 1
}

// Client KVDB客户端
type Client struct {
	config *ClientConfig
	conn   *grpc.ClientConn
	client pb.KVDBServiceClient
	
	// 订阅管理
	subscriptions    map[int32]*Subscription
	subscriptionsMux sync.RWMutex
	subscriptionID   int32
	
	connected int32
}

// NewClient 创建新的KVDB客户端
func NewClient(config *ClientConfig) *Client {
	if config == nil {
		config = DefaultClientConfig()
	}
	
	return &Client{
		config:        config,
		subscriptions: make(map[int32]*Subscription),
	}
}

// Connect 连接到服务器
func (c *Client) Connect(ctx context.Context) error {
	if c.config.Protocol != "grpc" {
		return fmt.Errorf("unsupported protocol: %s", c.config.Protocol)
	}
	
	opts := []grpc.DialOption{
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithDefaultCallOptions(
			grpc.MaxCallRecvMsgSize(c.config.MaxRecvMsgSize),
			grpc.MaxCallSendMsgSize(c.config.MaxSendMsgSize),
		),
	}
	
	if c.config.EnableCompression {
		opts = append(opts, grpc.WithDefaultCallOptions(grpc.UseCompressor("gzip")))
	}
	
	connectCtx, cancel := context.WithTimeout(ctx, c.config.ConnectionTimeout)
	defer cancel()
	
	conn, err := grpc.DialContext(connectCtx, c.config.ServerAddress, opts...)
	if err != nil {
		return fmt.Errorf("failed to connect: %w", err)
	}
	
	c.conn = conn
	c.client = pb.NewKVDBServiceClient(conn)
	
	// 测试连接
	_, err = c.client.GetStats(connectCtx, &pb.GetStatsRequest{})
	if err != nil {
		c.conn.Close()
		return fmt.Errorf("connection test failed: %w", err)
	}
	
	atomic.StoreInt32(&c.connected, 1)
	return nil
}

// Disconnect 断开连接
func (c *Client) Disconnect() error {
	if !atomic.CompareAndSwapInt32(&c.connected, 1, 0) {
		return nil
	}
	
	// 取消所有订阅
	c.subscriptionsMux.Lock()
	for _, sub := range c.subscriptions {
		sub.Cancel()
	}
	c.subscriptions = make(map[int32]*Subscription)
	c.subscriptionsMux.Unlock()
	
	if c.conn != nil {
		return c.conn.Close()
	}
	
	return nil
}

// IsConnected 检查连接状态
func (c *Client) IsConnected() bool {
	return atomic.LoadInt32(&c.connected) == 1
}

// Put 存储键值对
func (c *Client) Put(ctx context.Context, key, value string) error {
	if !c.IsConnected() {
		return fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.PutRequest{
		Key:   key,
		Value: value,
	}
	
	resp, err := c.client.Put(reqCtx, req)
	if err != nil {
		return fmt.Errorf("put operation failed: %w", err)
	}
	
	if !resp.Success {
		return fmt.Errorf("put operation failed: %s", resp.ErrorMessage)
	}
	
	return nil
}

// Get 获取键对应的值
func (c *Client) Get(ctx context.Context, key string) (string, bool, error) {
	if !c.IsConnected() {
		return "", false, fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.GetRequest{Key: key}
	
	resp, err := c.client.Get(reqCtx, req)
	if err != nil {
		return "", false, fmt.Errorf("get operation failed: %w", err)
	}
	
	if resp.ErrorMessage != "" {
		return "", false, fmt.Errorf("get operation failed: %s", resp.ErrorMessage)
	}
	
	return resp.Value, resp.Found, nil
}

// Delete 删除键
func (c *Client) Delete(ctx context.Context, key string) error {
	if !c.IsConnected() {
		return fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.DeleteRequest{Key: key}
	
	resp, err := c.client.Delete(reqCtx, req)
	if err != nil {
		return fmt.Errorf("delete operation failed: %w", err)
	}
	
	if !resp.Success {
		return fmt.Errorf("delete operation failed: %s", resp.ErrorMessage)
	}
	
	return nil
}

// BatchPut 批量存储
func (c *Client) BatchPut(ctx context.Context, pairs []KeyValue) error {
	if !c.IsConnected() {
		return fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.BatchPutRequest{}
	for _, pair := range pairs {
		req.Pairs = append(req.Pairs, &pb.KeyValue{
			Key:   pair.Key,
			Value: pair.Value,
		})
	}
	
	resp, err := c.client.BatchPut(reqCtx, req)
	if err != nil {
		return fmt.Errorf("batch put operation failed: %w", err)
	}
	
	if !resp.Success {
		return fmt.Errorf("batch put operation failed: %s", resp.ErrorMessage)
	}
	
	return nil
}

// BatchGet 批量获取
func (c *Client) BatchGet(ctx context.Context, keys []string) ([]KeyValue, error) {
	if !c.IsConnected() {
		return nil, fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.BatchGetRequest{Keys: keys}
	
	resp, err := c.client.BatchGet(reqCtx, req)
	if err != nil {
		return nil, fmt.Errorf("batch get operation failed: %w", err)
	}
	
	if resp.ErrorMessage != "" {
		return nil, fmt.Errorf("batch get operation failed: %s", resp.ErrorMessage)
	}
	
	var result []KeyValue
	for _, pair := range resp.Pairs {
		result = append(result, KeyValue{
			Key:   pair.Key,
			Value: pair.Value,
		})
	}
	
	return result, nil
}

// Scan 范围扫描
func (c *Client) Scan(ctx context.Context, options *ScanOptions) ([]KeyValue, error) {
	if !c.IsConnected() {
		return nil, fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.ScanRequest{
		StartKey: options.StartKey,
		EndKey:   options.EndKey,
		Limit:    options.Limit,
	}
	
	stream, err := c.client.Scan(reqCtx, req)
	if err != nil {
		return nil, fmt.Errorf("scan operation failed: %w", err)
	}
	
	var result []KeyValue
	for {
		resp, err := stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("scan stream error: %w", err)
		}
		
		result = append(result, KeyValue{
			Key:   resp.Key,
			Value: resp.Value,
		})
	}
	
	return result, nil
}

// PrefixScan 前缀扫描
func (c *Client) PrefixScan(ctx context.Context, prefix string, limit int32) ([]KeyValue, error) {
	if !c.IsConnected() {
		return nil, fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.PrefixScanRequest{
		Prefix: prefix,
		Limit:  limit,
	}
	
	stream, err := c.client.PrefixScan(reqCtx, req)
	if err != nil {
		return nil, fmt.Errorf("prefix scan operation failed: %w", err)
	}
	
	var result []KeyValue
	for {
		resp, err := stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("prefix scan stream error: %w", err)
		}
		
		result = append(result, KeyValue{
			Key:   resp.Key,
			Value: resp.Value,
		})
	}
	
	return result, nil
}

// CreateSnapshot 创建快照
func (c *Client) CreateSnapshot(ctx context.Context) (*Snapshot, error) {
	if !c.IsConnected() {
		return nil, fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.CreateSnapshotRequest{}
	
	resp, err := c.client.CreateSnapshot(reqCtx, req)
	if err != nil {
		return nil, fmt.Errorf("create snapshot operation failed: %w", err)
	}
	
	if resp.ErrorMessage != "" {
		return nil, fmt.Errorf("create snapshot operation failed: %s", resp.ErrorMessage)
	}
	
	return &Snapshot{ID: resp.SnapshotId}, nil
}

// ReleaseSnapshot 释放快照
func (c *Client) ReleaseSnapshot(ctx context.Context, snapshot *Snapshot) error {
	if !c.IsConnected() {
		return fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.ReleaseSnapshotRequest{SnapshotId: snapshot.ID}
	
	resp, err := c.client.ReleaseSnapshot(reqCtx, req)
	if err != nil {
		return fmt.Errorf("release snapshot operation failed: %w", err)
	}
	
	if !resp.Success {
		return fmt.Errorf("release snapshot operation failed: %s", resp.ErrorMessage)
	}
	
	return nil
}

// GetAtSnapshot 在快照上读取数据
func (c *Client) GetAtSnapshot(ctx context.Context, key string, snapshot *Snapshot) (string, bool, error) {
	if !c.IsConnected() {
		return "", false, fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.GetAtSnapshotRequest{
		Key:        key,
		SnapshotId: snapshot.ID,
	}
	
	resp, err := c.client.GetAtSnapshot(reqCtx, req)
	if err != nil {
		return "", false, fmt.Errorf("get at snapshot operation failed: %w", err)
	}
	
	if resp.ErrorMessage != "" {
		return "", false, fmt.Errorf("get at snapshot operation failed: %s", resp.ErrorMessage)
	}
	
	return resp.Value, resp.Found, nil
}

// Flush 刷新数据到磁盘
func (c *Client) Flush(ctx context.Context) error {
	if !c.IsConnected() {
		return fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.FlushRequest{}
	
	resp, err := c.client.Flush(reqCtx, req)
	if err != nil {
		return fmt.Errorf("flush operation failed: %w", err)
	}
	
	if !resp.Success {
		return fmt.Errorf("flush operation failed: %s", resp.ErrorMessage)
	}
	
	return nil
}

// Compact 压缩数据
func (c *Client) Compact(ctx context.Context) error {
	if !c.IsConnected() {
		return fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.CompactRequest{}
	
	resp, err := c.client.Compact(reqCtx, req)
	if err != nil {
		return fmt.Errorf("compact operation failed: %w", err)
	}
	
	if !resp.Success {
		return fmt.Errorf("compact operation failed: %s", resp.ErrorMessage)
	}
	
	return nil
}

// GetStats 获取数据库统计信息
func (c *Client) GetStats(ctx context.Context) (*DatabaseStats, error) {
	if !c.IsConnected() {
		return nil, fmt.Errorf("client not connected")
	}
	
	reqCtx, cancel := context.WithTimeout(ctx, c.config.RequestTimeout)
	defer cancel()
	
	req := &pb.GetStatsRequest{}
	
	resp, err := c.client.GetStats(reqCtx, req)
	if err != nil {
		return nil, fmt.Errorf("get stats operation failed: %w", err)
	}
	
	return &DatabaseStats{
		MemtableSize:    resp.MemtableSize,
		WALSize:         resp.WalSize,
		CacheHitRate:    resp.CacheHitRate,
		ActiveSnapshots: resp.ActiveSnapshots,
	}, nil
}

// Subscribe 订阅键变化事件
func (c *Client) Subscribe(ctx context.Context, pattern string, callback SubscriptionCallback, includeDeletes bool) (*Subscription, error) {
	if !c.IsConnected() {
		return nil, fmt.Errorf("client not connected")
	}
	
	subCtx, cancel := context.WithCancel(ctx)
	
	req := &pb.SubscribeRequest{
		KeyPattern:     pattern,
		IncludeDeletes: includeDeletes,
	}
	
	stream, err := c.client.Subscribe(subCtx, req)
	if err != nil {
		cancel()
		return nil, fmt.Errorf("subscribe operation failed: %w", err)
	}
	
	subID := atomic.AddInt32(&c.subscriptionID, 1)
	
	subscription := &Subscription{
		id:     subID,
		client: c,
		cancel: cancel,
		active: 1,
	}
	
	c.subscriptionsMux.Lock()
	c.subscriptions[subID] = subscription
	c.subscriptionsMux.Unlock()
	
	// 启动接收goroutine
	go func() {
		defer func() {
			subscription.Cancel()
		}()
		
		for {
			resp, err := stream.Recv()
			if err != nil {
				if err != io.EOF && subscription.IsActive() {
					// 可以添加错误处理逻辑
				}
				return
			}
			
			if subscription.IsActive() && callback != nil {
				event := &SubscriptionEvent{
					Key:       resp.Key,
					Value:     resp.Value,
					Operation: resp.Operation,
					Timestamp: resp.Timestamp,
				}
				callback(event)
			}
		}
	}()
	
	return subscription, nil
}

// removeSubscription 移除订阅
func (c *Client) removeSubscription(id int32) {
	c.subscriptionsMux.Lock()
	delete(c.subscriptions, id)
	c.subscriptionsMux.Unlock()
}

// Close 关闭客户端
func (c *Client) Close() error {
	return c.Disconnect()
}