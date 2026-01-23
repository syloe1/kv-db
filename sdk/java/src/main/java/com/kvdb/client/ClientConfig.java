package com.kvdb.client;

/**
 * KVDB客户端配置
 */
public class ClientConfig {
    
    private String serverAddress = "localhost:50051";
    private String protocol = "grpc";
    private long connectionTimeout = 5000; // ms
    private long requestTimeout = 30000; // ms
    private int maxRetries = 3;
    private boolean enableCompression = true;
    private String compressionType = "gzip";
    
    // 连接池配置
    private int maxConnections = 10;
    private int minConnections = 2;
    private long connectionIdleTimeout = 60000; // ms
    
    // gRPC特定配置
    private int maxInboundMessageSize = 64 * 1024 * 1024; // 64MB
    private long keepAliveTime = 30000; // ms
    private long keepAliveTimeout = 5000; // ms
    private boolean keepAliveWithoutCalls = false;
    
    // HTTP特定配置
    private String httpBaseUrl;
    private int httpConnectionPoolSize = 10;
    private long httpConnectionTimeout = 5000; // ms
    private long httpReadTimeout = 30000; // ms
    
    public ClientConfig() {
        // 默认HTTP URL基于gRPC地址
        updateHttpBaseUrl();
    }
    
    private void updateHttpBaseUrl() {
        if (httpBaseUrl == null && serverAddress != null) {
            String[] parts = serverAddress.split(":");
            if (parts.length == 2) {
                String host = parts[0];
                int port = Integer.parseInt(parts[1]) + 1000; // HTTP端口通常是gRPC端口+1000
                httpBaseUrl = "http://" + host + ":" + port;
            }
        }
    }
    
    // Getters and Setters
    
    public String getServerAddress() {
        return serverAddress;
    }
    
    public ClientConfig setServerAddress(String serverAddress) {
        this.serverAddress = serverAddress;
        updateHttpBaseUrl();
        return this;
    }
    
    public String getProtocol() {
        return protocol;
    }
    
    public ClientConfig setProtocol(String protocol) {
        this.protocol = protocol;
        return this;
    }
    
    public long getConnectionTimeout() {
        return connectionTimeout;
    }
    
    public ClientConfig setConnectionTimeout(long connectionTimeout) {
        this.connectionTimeout = connectionTimeout;
        return this;
    }
    
    public long getRequestTimeout() {
        return requestTimeout;
    }
    
    public ClientConfig setRequestTimeout(long requestTimeout) {
        this.requestTimeout = requestTimeout;
        return this;
    }
    
    public int getMaxRetries() {
        return maxRetries;
    }
    
    public ClientConfig setMaxRetries(int maxRetries) {
        this.maxRetries = maxRetries;
        return this;
    }
    
    public boolean isEnableCompression() {
        return enableCompression;
    }
    
    public ClientConfig setEnableCompression(boolean enableCompression) {
        this.enableCompression = enableCompression;
        return this;
    }
    
    public String getCompressionType() {
        return compressionType;
    }
    
    public ClientConfig setCompressionType(String compressionType) {
        this.compressionType = compressionType;
        return this;
    }
    
    public int getMaxConnections() {
        return maxConnections;
    }
    
    public ClientConfig setMaxConnections(int maxConnections) {
        this.maxConnections = maxConnections;
        return this;
    }
    
    public int getMinConnections() {
        return minConnections;
    }
    
    public ClientConfig setMinConnections(int minConnections) {
        this.minConnections = minConnections;
        return this;
    }
    
    public long getConnectionIdleTimeout() {
        return connectionIdleTimeout;
    }
    
    public ClientConfig setConnectionIdleTimeout(long connectionIdleTimeout) {
        this.connectionIdleTimeout = connectionIdleTimeout;
        return this;
    }
    
    public int getMaxInboundMessageSize() {
        return maxInboundMessageSize;
    }
    
    public ClientConfig setMaxInboundMessageSize(int maxInboundMessageSize) {
        this.maxInboundMessageSize = maxInboundMessageSize;
        return this;
    }
    
    public long getKeepAliveTime() {
        return keepAliveTime;
    }
    
    public ClientConfig setKeepAliveTime(long keepAliveTime) {
        this.keepAliveTime = keepAliveTime;
        return this;
    }
    
    public long getKeepAliveTimeout() {
        return keepAliveTimeout;
    }
    
    public ClientConfig setKeepAliveTimeout(long keepAliveTimeout) {
        this.keepAliveTimeout = keepAliveTimeout;
        return this;
    }
    
    public boolean isKeepAliveWithoutCalls() {
        return keepAliveWithoutCalls;
    }
    
    public ClientConfig setKeepAliveWithoutCalls(boolean keepAliveWithoutCalls) {
        this.keepAliveWithoutCalls = keepAliveWithoutCalls;
        return this;
    }
    
    public String getHttpBaseUrl() {
        return httpBaseUrl;
    }
    
    public ClientConfig setHttpBaseUrl(String httpBaseUrl) {
        this.httpBaseUrl = httpBaseUrl;
        return this;
    }
    
    public int getHttpConnectionPoolSize() {
        return httpConnectionPoolSize;
    }
    
    public ClientConfig setHttpConnectionPoolSize(int httpConnectionPoolSize) {
        this.httpConnectionPoolSize = httpConnectionPoolSize;
        return this;
    }
    
    public long getHttpConnectionTimeout() {
        return httpConnectionTimeout;
    }
    
    public ClientConfig setHttpConnectionTimeout(long httpConnectionTimeout) {
        this.httpConnectionTimeout = httpConnectionTimeout;
        return this;
    }
    
    public long getHttpReadTimeout() {
        return httpReadTimeout;
    }
    
    public ClientConfig setHttpReadTimeout(long httpReadTimeout) {
        this.httpReadTimeout = httpReadTimeout;
        return this;
    }
    
    @Override
    public String toString() {
        return "ClientConfig{" +
                "serverAddress='" + serverAddress + '\'' +
                ", protocol='" + protocol + '\'' +
                ", connectionTimeout=" + connectionTimeout +
                ", requestTimeout=" + requestTimeout +
                ", maxRetries=" + maxRetries +
                ", enableCompression=" + enableCompression +
                ", compressionType='" + compressionType + '\'' +
                ", maxConnections=" + maxConnections +
                ", minConnections=" + minConnections +
                ", connectionIdleTimeout=" + connectionIdleTimeout +
                ", maxInboundMessageSize=" + maxInboundMessageSize +
                ", keepAliveTime=" + keepAliveTime +
                ", keepAliveTimeout=" + keepAliveTimeout +
                ", keepAliveWithoutCalls=" + keepAliveWithoutCalls +
                ", httpBaseUrl='" + httpBaseUrl + '\'' +
                ", httpConnectionPoolSize=" + httpConnectionPoolSize +
                ", httpConnectionTimeout=" + httpConnectionTimeout +
                ", httpReadTimeout=" + httpReadTimeout +
                '}';
    }
}