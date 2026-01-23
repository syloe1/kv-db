#include "grpc_client.h"
#include <grpcpp/grpcpp.h>
#include <thread>
#include <chrono>

namespace kvdb {
namespace client {

class GRPCClientImpl : public KVDBClient {
public:
    explicit GRPCClientImpl(const ClientConfig& config) 
        : config_(config), connected_(false) {
        
        grpc::ChannelArguments args;
        args.SetMaxReceiveMessageSize(64 * 1024 * 1024); // 64MB
        args.SetMaxSendMessageSize(64 * 1024 * 1024);
        
        if (config_.enable_compression) {
            args.SetCompressionAlgorithm(GRPC_COMPRESS_GZIP);
        }
        
        channel_ = grpc::CreateCustomChannel(
            config_.server_address,
            grpc::InsecureChannelCredentials(),
            args
        );
        
        stub_ = kvdb::KVDBService::NewStub(channel_);
    }
    
    Result<bool> connect() override {
        auto deadline = std::chrono::system_clock::now() + 
                       std::chrono::milliseconds(config_.connection_timeout_ms);
        
        if (channel_->WaitForConnected(deadline)) {
            connected_ = true;
            return Result<bool>::ok(true);
        }
        
        return Result<bool>::error("Failed to connect to server");
    }
    
    void disconnect() override {
        connected_ = false;
        // gRPC channel will be cleaned up automatically
    }
    
    bool is_connected() const override {
        return connected_ && channel_->GetState(false) == GRPC_CHANNEL_READY;
    }
    
    Result<bool> put(const std::string& key, const std::string& value) override {
        kvdb::PutRequest request;
        request.set_key(key);
        request.set_value(value);
        
        kvdb::PutResponse response;
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->Put(&context, request, &response);
        
        if (!status.ok()) {
            return Result<bool>::error("gRPC error: " + status.error_message());
        }
        
        if (!response.success()) {
            return Result<bool>::error(response.error_message());
        }
        
        return Result<bool>::ok(true);
    }
    
    Result<std::string> get(const std::string& key) override {
        kvdb::GetRequest request;
        request.set_key(key);
        
        kvdb::GetResponse response;
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->Get(&context, request, &response);
        
        if (!status.ok()) {
            return Result<std::string>::error("gRPC error: " + status.error_message());
        }
        
        if (!response.found()) {
            return Result<std::string>::error("Key not found");
        }
        
        if (!response.error_message().empty()) {
            return Result<std::string>::error(response.error_message());
        }
        
        return Result<std::string>::ok(response.value());
    }
    
    Result<bool> del(const std::string& key) override {
        kvdb::DeleteRequest request;
        request.set_key(key);
        
        kvdb::DeleteResponse response;
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->Delete(&context, request, &response);
        
        if (!status.ok()) {
            return Result<bool>::error("gRPC error: " + status.error_message());
        }
        
        if (!response.success()) {
            return Result<bool>::error(response.error_message());
        }
        
        return Result<bool>::ok(true);
    }
    
    std::future<Result<bool>> put_async(const std::string& key, const std::string& value) override {
        return std::async(std::launch::async, [this, key, value]() {
            return put(key, value);
        });
    }
    
    std::future<Result<std::string>> get_async(const std::string& key) override {
        return std::async(std::launch::async, [this, key]() {
            return get(key);
        });
    }
    
    std::future<Result<bool>> del_async(const std::string& key) override {
        return std::async(std::launch::async, [this, key]() {
            return del(key);
        });
    }
    
    Result<bool> batch_put(const std::vector<KeyValue>& pairs) override {
        kvdb::BatchPutRequest request;
        
        for (const auto& pair : pairs) {
            auto* kv = request.add_pairs();
            kv->set_key(pair.key);
            kv->set_value(pair.value);
        }
        
        kvdb::BatchPutResponse response;
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->BatchPut(&context, request, &response);
        
        if (!status.ok()) {
            return Result<bool>::error("gRPC error: " + status.error_message());
        }
        
        if (!response.success()) {
            return Result<bool>::error(response.error_message());
        }
        
        return Result<bool>::ok(true);
    }
    
    Result<std::vector<KeyValue>> batch_get(const std::vector<std::string>& keys) override {
        kvdb::BatchGetRequest request;
        
        for (const auto& key : keys) {
            request.add_keys(key);
        }
        
        kvdb::BatchGetResponse response;
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->BatchGet(&context, request, &response);
        
        if (!status.ok()) {
            return Result<std::vector<KeyValue>>::error("gRPC error: " + status.error_message());
        }
        
        if (!response.error_message().empty()) {
            return Result<std::vector<KeyValue>>::error(response.error_message());
        }
        
        std::vector<KeyValue> result;
        for (const auto& pair : response.pairs()) {
            result.emplace_back(pair.key(), pair.value());
        }
        
        return Result<std::vector<KeyValue>>::ok(std::move(result));
    }
    
    Result<std::vector<KeyValue>> scan(const ScanOptions& options) override {
        kvdb::ScanRequest request;
        request.set_start_key(options.start_key);
        request.set_end_key(options.end_key);
        request.set_limit(options.limit);
        
        grpc::ClientContext context;
        set_timeout(context);
        
        auto reader = stub_->Scan(&context, request);
        
        std::vector<KeyValue> result;
        kvdb::ScanResponse response;
        
        while (reader->Read(&response)) {
            result.emplace_back(response.key(), response.value());
        }
        
        grpc::Status status = reader->Finish();
        if (!status.ok()) {
            return Result<std::vector<KeyValue>>::error("gRPC error: " + status.error_message());
        }
        
        return Result<std::vector<KeyValue>>::ok(std::move(result));
    }
    
    Result<std::vector<KeyValue>> prefix_scan(const std::string& prefix, int limit) override {
        kvdb::PrefixScanRequest request;
        request.set_prefix(prefix);
        request.set_limit(limit);
        
        grpc::ClientContext context;
        set_timeout(context);
        
        auto reader = stub_->PrefixScan(&context, request);
        
        std::vector<KeyValue> result;
        kvdb::PrefixScanResponse response;
        
        while (reader->Read(&response)) {
            result.emplace_back(response.key(), response.value());
        }
        
        grpc::Status status = reader->Finish();
        if (!status.ok()) {
            return Result<std::vector<KeyValue>>::error("gRPC error: " + status.error_message());
        }
        
        return Result<std::vector<KeyValue>>::ok(std::move(result));
    }
    
    Result<std::shared_ptr<Snapshot>> create_snapshot() override {
        kvdb::CreateSnapshotRequest request;
        kvdb::CreateSnapshotResponse response;
        
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->CreateSnapshot(&context, request, &response);
        
        if (!status.ok()) {
            return Result<std::shared_ptr<Snapshot>>::error("gRPC error: " + status.error_message());
        }
        
        if (!response.error_message().empty()) {
            return Result<std::shared_ptr<Snapshot>>::error(response.error_message());
        }
        
        auto snapshot = std::make_shared<Snapshot>(response.snapshot_id());
        return Result<std::shared_ptr<Snapshot>>::ok(snapshot);
    }
    
    Result<bool> release_snapshot(std::shared_ptr<Snapshot> snapshot) override {
        kvdb::ReleaseSnapshotRequest request;
        request.set_snapshot_id(snapshot->id());
        
        kvdb::ReleaseSnapshotResponse response;
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->ReleaseSnapshot(&context, request, &response);
        
        if (!status.ok()) {
            return Result<bool>::error("gRPC error: " + status.error_message());
        }
        
        if (!response.success()) {
            return Result<bool>::error(response.error_message());
        }
        
        return Result<bool>::ok(true);
    }
    
    Result<std::string> get_at_snapshot(const std::string& key, std::shared_ptr<Snapshot> snapshot) override {
        kvdb::GetAtSnapshotRequest request;
        request.set_key(key);
        request.set_snapshot_id(snapshot->id());
        
        kvdb::GetAtSnapshotResponse response;
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->GetAtSnapshot(&context, request, &response);
        
        if (!status.ok()) {
            return Result<std::string>::error("gRPC error: " + status.error_message());
        }
        
        if (!response.found()) {
            return Result<std::string>::error("Key not found");
        }
        
        if (!response.error_message().empty()) {
            return Result<std::string>::error(response.error_message());
        }
        
        return Result<std::string>::ok(response.value());
    }
    
    Result<bool> flush() override {
        kvdb::FlushRequest request;
        kvdb::FlushResponse response;
        
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->Flush(&context, request, &response);
        
        if (!status.ok()) {
            return Result<bool>::error("gRPC error: " + status.error_message());
        }
        
        if (!response.success()) {
            return Result<bool>::error(response.error_message());
        }
        
        return Result<bool>::ok(true);
    }
    
    Result<bool> compact() override {
        kvdb::CompactRequest request;
        kvdb::CompactResponse response;
        
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->Compact(&context, request, &response);
        
        if (!status.ok()) {
            return Result<bool>::error("gRPC error: " + status.error_message());
        }
        
        if (!response.success()) {
            return Result<bool>::error(response.error_message());
        }
        
        return Result<bool>::ok(true);
    }
    
    Result<std::map<std::string, std::string>> get_stats() override {
        kvdb::GetStatsRequest request;
        kvdb::GetStatsResponse response;
        
        grpc::ClientContext context;
        set_timeout(context);
        
        grpc::Status status = stub_->GetStats(&context, request, &response);
        
        if (!status.ok()) {
            return Result<std::map<std::string, std::string>>::error("gRPC error: " + status.error_message());
        }
        
        std::map<std::string, std::string> stats;
        stats["memtable_size"] = std::to_string(response.memtable_size());
        stats["wal_size"] = std::to_string(response.wal_size());
        stats["cache_hit_rate"] = std::to_string(response.cache_hit_rate());
        stats["active_snapshots"] = std::to_string(response.active_snapshots());
        
        return Result<std::map<std::string, std::string>>::ok(std::move(stats));
    }
    
    Result<std::shared_ptr<Subscription>> subscribe(const std::string& pattern, 
                                                   SubscriptionCallback callback,
                                                   bool include_deletes) override {
        // 实现订阅功能
        auto subscription = std::make_shared<GRPCSubscription>(
            stub_, pattern, callback, include_deletes);
        
        if (subscription->start()) {
            return Result<std::shared_ptr<Subscription>>::ok(subscription);
        } else {
            return Result<std::shared_ptr<Subscription>>::error("Failed to start subscription");
        }
    }

private:
    void set_timeout(grpc::ClientContext& context) {
        auto deadline = std::chrono::system_clock::now() + 
                       std::chrono::milliseconds(config_.request_timeout_ms);
        context.set_deadline(deadline);
    }
    
    ClientConfig config_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<kvdb::KVDBService::Stub> stub_;
    std::atomic<bool> connected_;
};

// 订阅实现
class GRPCSubscription : public Subscription {
public:
    GRPCSubscription(std::unique_ptr<kvdb::KVDBService::Stub>& stub,
                     const std::string& pattern,
                     SubscriptionCallback callback,
                     bool include_deletes)
        : stub_(stub), pattern_(pattern), callback_(callback), 
          include_deletes_(include_deletes), active_(false) {}
    
    ~GRPCSubscription() {
        cancel();
    }
    
    bool start() {
        kvdb::SubscribeRequest request;
        request.set_key_pattern(pattern_);
        request.set_include_deletes(include_deletes_);
        
        context_ = std::make_unique<grpc::ClientContext>();
        reader_ = stub_->Subscribe(context_.get(), request);
        
        active_ = true;
        
        // 启动读取线程
        read_thread_ = std::thread([this]() {
            kvdb::SubscribeResponse response;
            while (active_ && reader_->Read(&response)) {
                if (callback_) {
                    callback_(response.key(), response.value(), response.operation());
                }
            }
            active_ = false;
        });
        
        return true;
    }
    
    void cancel() override {
        if (active_) {
            active_ = false;
            if (context_) {
                context_->TryCancel();
            }
            if (read_thread_.joinable()) {
                read_thread_.join();
            }
        }
    }
    
    bool is_active() const override {
        return active_;
    }

private:
    std::unique_ptr<kvdb::KVDBService::Stub>& stub_;
    std::string pattern_;
    SubscriptionCallback callback_;
    bool include_deletes_;
    std::atomic<bool> active_;
    
    std::unique_ptr<grpc::ClientContext> context_;
    std::unique_ptr<grpc::ClientReader<kvdb::SubscribeResponse>> reader_;
    std::thread read_thread_;
};

std::unique_ptr<KVDBClient> ClientFactory::create_grpc_client(const ClientConfig& config) {
    return std::make_unique<GRPCClientImpl>(config);
}

} // namespace client
} // namespace kvdb