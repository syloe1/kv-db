#pragma once
#include "db/kv_db.h"
#include "kvdb.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_set>

class KVDBServiceImpl final : public kvdb::KVDBService::Service {
public:
    explicit KVDBServiceImpl(KVDB& db);
    ~KVDBServiceImpl();

    // 基本操作
    grpc::Status Put(grpc::ServerContext* context,
                     const kvdb::PutRequest* request,
                     kvdb::PutResponse* response) override;

    grpc::Status Get(grpc::ServerContext* context,
                     const kvdb::GetRequest* request,
                     kvdb::GetResponse* response) override;

    grpc::Status Delete(grpc::ServerContext* context,
                        const kvdb::DeleteRequest* request,
                        kvdb::DeleteResponse* response) override;

    // 批量操作
    grpc::Status BatchPut(grpc::ServerContext* context,
                          const kvdb::BatchPutRequest* request,
                          kvdb::BatchPutResponse* response) override;

    grpc::Status BatchGet(grpc::ServerContext* context,
                          const kvdb::BatchGetRequest* request,
                          kvdb::BatchGetResponse* response) override;

    // 扫描操作
    grpc::Status Scan(grpc::ServerContext* context,
                      const kvdb::ScanRequest* request,
                      grpc::ServerWriter<kvdb::ScanResponse>* writer) override;

    grpc::Status PrefixScan(grpc::ServerContext* context,
                            const kvdb::PrefixScanRequest* request,
                            grpc::ServerWriter<kvdb::PrefixScanResponse>* writer) override;

    // 快照操作
    grpc::Status CreateSnapshot(grpc::ServerContext* context,
                                const kvdb::CreateSnapshotRequest* request,
                                kvdb::CreateSnapshotResponse* response) override;

    grpc::Status ReleaseSnapshot(grpc::ServerContext* context,
                                 const kvdb::ReleaseSnapshotRequest* request,
                                 kvdb::ReleaseSnapshotResponse* response) override;

    grpc::Status GetAtSnapshot(grpc::ServerContext* context,
                               const kvdb::GetAtSnapshotRequest* request,
                               kvdb::GetAtSnapshotResponse* response) override;

    // 管理操作
    grpc::Status Flush(grpc::ServerContext* context,
                       const kvdb::FlushRequest* request,
                       kvdb::FlushResponse* response) override;

    grpc::Status Compact(grpc::ServerContext* context,
                         const kvdb::CompactRequest* request,
                         kvdb::CompactResponse* response) override;

    grpc::Status GetStats(grpc::ServerContext* context,
                          const kvdb::GetStatsRequest* request,
                          kvdb::GetStatsResponse* response) override;

    grpc::Status SetCompactionStrategy(grpc::ServerContext* context,
                                       const kvdb::SetCompactionStrategyRequest* request,
                                       kvdb::SetCompactionStrategyResponse* response) override;

    // 实时订阅
    grpc::Status Subscribe(grpc::ServerContext* context,
                           const kvdb::SubscribeRequest* request,
                           grpc::ServerWriter<kvdb::SubscribeResponse>* writer) override;

private:
    KVDB& db_;
    
    // 订阅管理
    struct Subscription {
        std::string pattern;
        bool include_deletes;
        grpc::ServerWriter<kvdb::SubscribeResponse>* writer;
        std::atomic<bool> active{true};
    };
    
    std::mutex subscriptions_mutex_;
    std::vector<std::shared_ptr<Subscription>> subscriptions_;
    
    // 通知订阅者
    void notify_subscribers(const std::string& key, const std::string& value, const std::string& operation);
    bool matches_pattern(const std::string& key, const std::string& pattern);
    
    // 辅助方法
    CompactionStrategyType convert_strategy_type(kvdb::CompactionStrategyType proto_type);
    kvdb::CompactionStrategyType convert_strategy_type(CompactionStrategyType internal_type);
};

class GRPCServer {
public:
    explicit GRPCServer(KVDB& db, const std::string& server_address = "0.0.0.0:50051");
    ~GRPCServer();

    void start();
    void stop();
    void wait_for_shutdown();

private:
    KVDB& db_;
    std::string server_address_;
    std::unique_ptr<grpc::Server> server_;
    std::unique_ptr<KVDBServiceImpl> service_;
    std::atomic<bool> running_{false};
};