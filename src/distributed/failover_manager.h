#pragma once

#include "distributed_types.h"
#include "shard_manager.h"
#include "load_balancer.h"
#include <thread>
#include <atomic>
#include <condition_variable>
#include <unordered_set>

namespace kvdb {
namespace distributed {

class FailoverManager {
public:
    FailoverManager(ShardManager* shard_manager, LoadBalancer* load_balancer);
    ~FailoverManager();
    
    // 故障转移管理
    bool start_monitoring();
    bool stop_monitoring();
    bool is_monitoring() const;
    
    // 故障检测
    bool detect_node_failure(const std::string& node_id);
    bool detect_shard_failure(const std::string& shard_id);
    std::vector<std::string> get_failed_nodes() const;
    std::vector<std::string> get_failed_shards() const;
    
    // 故障恢复
    bool recover_node(const std::string& node_id);
    bool recover_shard(const std::string& shard_id);
    bool promote_replica_to_primary(const std::string& shard_id, const std::string& new_primary);
    
    // 自动故障转移
    void set_auto_failover(bool enabled);
    bool is_auto_failover_enabled() const;
    void set_failover_timeout(int seconds);
    int get_failover_timeout() const;
    
    // 故障转移状态
    FailoverState get_failover_state() const;
    std::string get_failover_status() const;
    
    // 健康检查配置
    void set_health_check_interval(int seconds);
    void set_failure_threshold(int count);
    void set_recovery_threshold(int count);
    
private:
    ShardManager* shard_manager_;
    LoadBalancer* load_balancer_;
    
    // 监控线程
    std::thread monitor_thread_;
    std::atomic<bool> monitoring_{false};
    std::atomic<bool> stop_requested_{false};
    std::mutex monitor_mutex_;
    std::condition_variable monitor_cv_;
    
    // 故障转移配置
    std::atomic<bool> auto_failover_enabled_{true};
    std::atomic<int> failover_timeout_{30};  // 秒
    std::atomic<int> health_check_interval_{5};  // 秒
    std::atomic<int> failure_threshold_{3};  // 连续失败次数
    std::atomic<int> recovery_threshold_{3};  // 连续成功次数
    
    // 故障状态
    mutable std::mutex state_mutex_;
    FailoverState current_state_{FailoverState::NORMAL};
    std::string status_message_;
    std::unordered_map<std::string, int> node_failure_counts_;
    std::unordered_map<std::string, int> node_recovery_counts_;
    std::unordered_set<std::string> failed_nodes_;
    std::unordered_set<std::string> failed_shards_;
    
    // 监控函数
    void monitor_loop();
    void check_node_health();
    void check_shard_health();
    void perform_auto_failover();
    
    // 故障处理
    void handle_node_failure(const std::string& node_id);
    void handle_shard_failure(const std::string& shard_id);
    void handle_node_recovery(const std::string& node_id);
    void handle_shard_recovery(const std::string& shard_id);
    
    // 工具函数
    bool ping_node(const std::string& node_id);
    bool check_shard_availability(const std::string& shard_id);
    std::string find_best_replica_for_promotion(const std::string& shard_id);
    void update_state(FailoverState new_state, const std::string& message);
};

} // namespace distributed
} // namespace kvdb