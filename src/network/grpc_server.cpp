#include "network/grpc_server.h"
#include <iostream>
#include <chrono>
#include <regex>

KVDBServiceImpl::KVDBServiceImpl(KVDB& db) : db_(db) {
}

KVDBServiceImpl::~KVDBServiceImpl() {
    // 停止所有订阅
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    for (auto& sub : subscriptions_) {
        sub->active = false;
    }
}

grpc::Status KVDBServiceImpl::Put(grpc::ServerContext* context,
                                  const kvdb::PutRequest* request,
                                  kvdb::PutResponse* response) {
    try {
        bool success = db_.put(request->key(), request->value());
        response->set_success(success);
        
        // 通知订阅者
        notify_subscribers(request->key(), request->value(), "PUT");
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::Get(grpc::ServerContext* context,
                                  const kvdb::GetRequest* request,
                                  kvdb::GetResponse* response) {
    try {
        std::string value;
        bool found = db_.get(request->key(), value);
        response->set_found(found);
        if (found) {
            response->set_value(value);
        }
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_found(false);
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::Delete(grpc::ServerContext* context,
                                     const kvdb::DeleteRequest* request,
                                     kvdb::DeleteResponse* response) {
    try {
        bool success = db_.del(request->key());
        response->set_success(success);
        
        // 通知订阅者
        notify_subscribers(request->key(), "", "DELETE");
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::BatchPut(grpc::ServerContext* context,
                                       const kvdb::BatchPutRequest* request,
                                       kvdb::BatchPutResponse* response) {
    try {
        int processed = 0;
        for (const auto& pair : request->pairs()) {
            if (db_.put(pair.key(), pair.value())) {
                processed++;
                notify_subscribers(pair.key(), pair.value(), "PUT");
            }
        }
        response->set_success(processed == request->pairs_size());
        response->set_processed_count(processed);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::BatchGet(grpc::ServerContext* context,
                                       const kvdb::BatchGetRequest* request,
                                       kvdb::BatchGetResponse* response) {
    try {
        for (const auto& key : request->keys()) {
            std::string value;
            if (db_.get(key, value)) {
                auto* pair = response->add_pairs();
                pair->set_key(key);
                pair->set_value(value);
            }
        }
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::Scan(grpc::ServerContext* context,
                                   const kvdb::ScanRequest* request,
                                   grpc::ServerWriter<kvdb::ScanResponse>* writer) {
    try {
        Snapshot snap = db_.get_snapshot();
        auto iter = db_.new_iterator(snap);
        
        int count = 0;
        int limit = request->limit() > 0 ? request->limit() : INT_MAX;
        
        for (iter->seek(request->start_key()); 
             iter->valid() && iter->key() <= request->end_key() && count < limit; 
             iter->next()) {
            
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
            
            std::string value = iter->value();
            if (!value.empty()) {
                kvdb::ScanResponse response;
                response.set_key(iter->key());
                response.set_value(value);
                writer->Write(response);
                count++;
            }
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status KVDBServiceImpl::PrefixScan(grpc::ServerContext* context,
                                          const kvdb::PrefixScanRequest* request,
                                          grpc::ServerWriter<kvdb::PrefixScanResponse>* writer) {
    try {
        Snapshot snap = db_.get_snapshot();
        auto iter = db_.new_prefix_iterator(snap, request->prefix());
        
        int count = 0;
        int limit = request->limit() > 0 ? request->limit() : INT_MAX;
        
        while (iter->valid() && count < limit) {
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
            
            std::string key = iter->key();
            std::string value = iter->value();
            
            // 检查前缀匹配
            if (key.size() < request->prefix().size() || 
                key.substr(0, request->prefix().size()) != request->prefix()) {
                break;
            }
            
            if (!value.empty()) {
                kvdb::PrefixScanResponse response;
                response.set_key(key);
                response.set_value(value);
                writer->Write(response);
                count++;
            }
            iter->next();
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status KVDBServiceImpl::CreateSnapshot(grpc::ServerContext* context,
                                              const kvdb::CreateSnapshotRequest* request,
                                              kvdb::CreateSnapshotResponse* response) {
    try {
        Snapshot snap = db_.get_snapshot();
        response->set_snapshot_id(snap.seq);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::ReleaseSnapshot(grpc::ServerContext* context,
                                               const kvdb::ReleaseSnapshotRequest* request,
                                               kvdb::ReleaseSnapshotResponse* response) {
    try {
        Snapshot snap(request->snapshot_id());
        db_.release_snapshot(snap);
        response->set_success(true);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::GetAtSnapshot(grpc::ServerContext* context,
                                             const kvdb::GetAtSnapshotRequest* request,
                                             kvdb::GetAtSnapshotResponse* response) {
    try {
        Snapshot snap(request->snapshot_id());
        std::string value;
        bool found = db_.get(request->key(), snap, value);
        response->set_found(found);
        if (found) {
            response->set_value(value);
        }
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_found(false);
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::Flush(grpc::ServerContext* context,
                                    const kvdb::FlushRequest* request,
                                    kvdb::FlushResponse* response) {
    try {
        db_.flush();
        response->set_success(true);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::Compact(grpc::ServerContext* context,
                                      const kvdb::CompactRequest* request,
                                      kvdb::CompactResponse* response) {
    try {
        db_.compact();
        response->set_success(true);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::GetStats(grpc::ServerContext* context,
                                       const kvdb::GetStatsRequest* request,
                                       kvdb::GetStatsResponse* response) {
    try {
        response->set_memtable_size(db_.get_memtable_size());
        response->set_wal_size(db_.get_wal_size());
        response->set_cache_hit_rate(db_.get_cache_hit_rate());
        
        const auto& stats = db_.get_compaction_stats();
        auto* compaction_stats = response->mutable_compaction_stats();
        compaction_stats->set_total_compactions(stats.total_compactions);
        compaction_stats->set_bytes_read(stats.bytes_read);
        compaction_stats->set_bytes_written(stats.bytes_written);
        compaction_stats->set_write_amplification(stats.write_amplification);
        compaction_stats->set_total_time_ms(stats.total_time.count());
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status KVDBServiceImpl::SetCompactionStrategy(grpc::ServerContext* context,
                                                     const kvdb::SetCompactionStrategyRequest* request,
                                                     kvdb::SetCompactionStrategyResponse* response) {
    try {
        CompactionStrategyType strategy = convert_strategy_type(request->strategy());
        db_.set_compaction_strategy(strategy);
        response->set_success(true);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status KVDBServiceImpl::Subscribe(grpc::ServerContext* context,
                                        const kvdb::SubscribeRequest* request,
                                        grpc::ServerWriter<kvdb::SubscribeResponse>* writer) {
    auto subscription = std::make_shared<Subscription>();
    subscription->pattern = request->key_pattern();
    subscription->include_deletes = request->include_deletes();
    subscription->writer = writer;
    
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscriptions_.push_back(subscription);
    }
    
    // 保持连接活跃，直到客户端断开
    while (!context->IsCancelled() && subscription->active) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 清理订阅
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscriptions_.erase(
            std::remove_if(subscriptions_.begin(), subscriptions_.end(),
                [subscription](const std::shared_ptr<Subscription>& shared_sub) {
                    return !shared_sub || shared_sub.get() == subscription.get();
                }),
            subscriptions_.end());
    }
    
    return grpc::Status::OK;
}

void KVDBServiceImpl::notify_subscribers(const std::string& key, const std::string& value, const std::string& operation) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        auto& sub = *it;
        
        if (!sub->active) {
            it = subscriptions_.erase(it);
            continue;
        }
        
        if (matches_pattern(key, sub->pattern) && 
            (sub->include_deletes || operation != "DELETE")) {
            
            kvdb::SubscribeResponse response;
            response.set_key(key);
            response.set_value(value);
            response.set_operation(operation);
            response.set_timestamp(now);
            
            try {
                sub->writer->Write(response);
            } catch (...) {
                sub->active = false;
                it = subscriptions_.erase(it);
                continue;
            }
        }
        
        ++it;
    }
}

bool KVDBServiceImpl::matches_pattern(const std::string& key, const std::string& pattern) {
    if (pattern == "*") return true;
    if (pattern.empty()) return key.empty();
    
    // 简单的通配符匹配
    try {
        std::string regex_pattern = pattern;
        std::replace(regex_pattern.begin(), regex_pattern.end(), '*', '.');
        regex_pattern = "^" + regex_pattern + "$";
        std::regex re(regex_pattern);
        return std::regex_match(key, re);
    } catch (...) {
        // 如果正则表达式无效，使用前缀匹配
        return key.find(pattern) == 0;
    }
}

CompactionStrategyType KVDBServiceImpl::convert_strategy_type(kvdb::CompactionStrategyType proto_type) {
    switch (proto_type) {
        case kvdb::LEVELED: return CompactionStrategyType::LEVELED;
        case kvdb::TIERED: return CompactionStrategyType::TIERED;
        case kvdb::SIZE_TIERED: return CompactionStrategyType::SIZE_TIERED;
        case kvdb::TIME_WINDOW: return CompactionStrategyType::TIME_WINDOW;
        default: return CompactionStrategyType::LEVELED;
    }
}

kvdb::CompactionStrategyType KVDBServiceImpl::convert_strategy_type(CompactionStrategyType internal_type) {
    switch (internal_type) {
        case CompactionStrategyType::LEVELED: return kvdb::LEVELED;
        case CompactionStrategyType::TIERED: return kvdb::TIERED;
        case CompactionStrategyType::SIZE_TIERED: return kvdb::SIZE_TIERED;
        case CompactionStrategyType::TIME_WINDOW: return kvdb::TIME_WINDOW;
        default: return kvdb::LEVELED;
    }
}

// GRPCServer 实现
GRPCServer::GRPCServer(KVDB& db, const std::string& server_address)
    : db_(db), server_address_(server_address) {
    service_ = std::make_unique<KVDBServiceImpl>(db_);
}

GRPCServer::~GRPCServer() {
    stop();
}

void GRPCServer::start() {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    
    server_ = builder.BuildAndStart();
    running_ = true;
    
    std::cout << "[gRPC] Server listening on " << server_address_ << std::endl;
}

void GRPCServer::stop() {
    if (server_ && running_) {
        server_->Shutdown();
        running_ = false;
        std::cout << "[gRPC] Server stopped" << std::endl;
    }
}

void GRPCServer::wait_for_shutdown() {
    if (server_) {
        server_->Wait();
    }
}