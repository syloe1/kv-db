#include "simple_raft_state_machine.h"
#include <iostream>
#include <sstream>
#include <algorithm>

SimpleRaftStateMachine::SimpleRaftStateMachine() : last_applied_index_(0) {}

std::string SimpleRaftStateMachine::apply(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    try {
        // 解析命令
        Command cmd = Command::parse(entry.command);
        
        std::string result;
        if (cmd.operation == "SET") {
            result = execute_set(cmd.key, cmd.value);
        } else if (cmd.operation == "GET") {
            result = execute_get(cmd.key);
        } else if (cmd.operation == "DELETE") {
            result = execute_delete(cmd.key);
        } else {
            result = "ERROR: Unknown operation: " + cmd.operation;
        }
        
        std::cout << "StateMachine: Applied command '" << entry.command 
                  << "' -> " << result << std::endl;
        
        return result;
        
    } catch (const std::exception& e) {
        std::string error = "ERROR: " + std::string(e.what());
        std::cout << "StateMachine: Error applying command '" << entry.command 
                  << "': " << error << std::endl;
        return error;
    }
}

std::vector<uint8_t> SimpleRaftStateMachine::create_snapshot() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    std::ostringstream oss;
    
    // 序列化最后应用的索引
    oss << last_applied_index_ << "\n";
    
    // 序列化键值对
    oss << kv_store_.size() << "\n";
    for (const auto& pair : kv_store_) {
        oss << pair.first.length() << " " << pair.first << " "
            << pair.second.length() << " " << pair.second << "\n";
    }
    
    std::string snapshot_str = oss.str();
    return std::vector<uint8_t>(snapshot_str.begin(), snapshot_str.end());
}

bool SimpleRaftStateMachine::restore_snapshot(const std::vector<uint8_t>& snapshot_data) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    try {
        std::string snapshot_str(snapshot_data.begin(), snapshot_data.end());
        std::istringstream iss(snapshot_str);
        
        // 反序列化最后应用的索引
        iss >> last_applied_index_;
        
        // 反序列化键值对
        size_t kv_count;
        iss >> kv_count;
        
        kv_store_.clear();
        for (size_t i = 0; i < kv_count; i++) {
            size_t key_len, value_len;
            std::string key, value;
            
            iss >> key_len;
            iss.ignore(); // 忽略空格
            key.resize(key_len);
            iss.read(&key[0], key_len);
            
            iss >> value_len;
            iss.ignore(); // 忽略空格
            value.resize(value_len);
            iss.read(&value[0], value_len);
            
            kv_store_[key] = value;
        }
        
        std::cout << "StateMachine: Restored snapshot with " << kv_count 
                  << " key-value pairs, last_applied_index: " << last_applied_index_ << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "StateMachine: Error restoring snapshot: " << e.what() << std::endl;
        return false;
    }
}

uint64_t SimpleRaftStateMachine::get_last_applied_index() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_applied_index_;
}

void SimpleRaftStateMachine::set_last_applied_index(uint64_t index) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_applied_index_ = index;
}

std::string SimpleRaftStateMachine::get(const std::string& key) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = kv_store_.find(key);
    return (it != kv_store_.end()) ? it->second : "";
}

size_t SimpleRaftStateMachine::size() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return kv_store_.size();
}

std::vector<std::pair<std::string, std::string>> SimpleRaftStateMachine::get_all() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& pair : kv_store_) {
        result.push_back(pair);
    }
    return result;
}

SimpleRaftStateMachine::Command SimpleRaftStateMachine::Command::parse(const std::string& command_str) {
    Command cmd;
    std::istringstream iss(command_str);
    
    iss >> cmd.operation;
    
    if (cmd.operation == "SET") {
        iss >> cmd.key >> cmd.value;
    } else if (cmd.operation == "GET" || cmd.operation == "DELETE") {
        iss >> cmd.key;
    }
    
    // 转换为大写
    std::transform(cmd.operation.begin(), cmd.operation.end(), 
                   cmd.operation.begin(), ::toupper);
    
    return cmd;
}

std::string SimpleRaftStateMachine::execute_set(const std::string& key, const std::string& value) {
    kv_store_[key] = value;
    return "OK";
}

std::string SimpleRaftStateMachine::execute_get(const std::string& key) {
    auto it = kv_store_.find(key);
    if (it != kv_store_.end()) {
        return it->second;
    } else {
        return "NOT_FOUND";
    }
}

std::string SimpleRaftStateMachine::execute_delete(const std::string& key) {
    auto it = kv_store_.find(key);
    if (it != kv_store_.end()) {
        kv_store_.erase(it);
        return "OK";
    } else {
        return "NOT_FOUND";
    }
}