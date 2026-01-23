#pragma once

#include "raft_node.h"
#include <unordered_map>
#include <mutex>
#include <sstream>

// 简单的键值存储状态机实现
class SimpleRaftStateMachine : public RaftStateMachine {
public:
    SimpleRaftStateMachine();
    ~SimpleRaftStateMachine() override = default;
    
    // 应用日志条目
    std::string apply(const LogEntry& entry) override;
    
    // 快照操作
    std::vector<uint8_t> create_snapshot() override;
    bool restore_snapshot(const std::vector<uint8_t>& snapshot_data) override;
    
    // 状态查询
    uint64_t get_last_applied_index() const override;
    void set_last_applied_index(uint64_t index) override;
    
    // 额外的查询方法
    std::string get(const std::string& key) const;
    size_t size() const;
    std::vector<std::pair<std::string, std::string>> get_all() const;

private:
    mutable std::mutex state_mutex_;
    std::unordered_map<std::string, std::string> kv_store_;
    uint64_t last_applied_index_;
    
    // 命令解析
    struct Command {
        std::string operation; // "SET", "GET", "DELETE"
        std::string key;
        std::string value;
        
        static Command parse(const std::string& command_str);
    };
    
    // 操作实现
    std::string execute_set(const std::string& key, const std::string& value);
    std::string execute_get(const std::string& key);
    std::string execute_delete(const std::string& key);
};