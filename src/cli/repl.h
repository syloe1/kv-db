#pragma once
#include "db/kv_db.h"
#ifdef ENABLE_NETWORK
#include "network/network_server.h"
#endif
#include <string>
#include <vector>
#include <memory>

class REPL {
public:
    explicit REPL(KVDB& db);
    ~REPL();
    void run();

private:
    KVDB& db_;
#ifdef ENABLE_NETWORK
    std::unique_ptr<NetworkServer> network_server_;  // 网络服务器
#endif
    std::vector<uint64_t> active_snapshots_; // 存储 snapshot seq，用于管理
    
    std::vector<std::string> split(const std::string& line);
    void execute_command(const std::vector<std::string>& tokens);
    
    // readline 相关
    char* read_input(const char* prompt);
    void setup_readline();
    
    // 命令补全相关（readline 函数指针类型）
    static char** command_completion(const char* text, int start, int end);
    static char* command_generator(const char* text, int state);
    static std::vector<std::string> commands_;
    
    // 命令处理函数
    void cmd_put(const std::vector<std::string>& tokens);
    void cmd_get(const std::vector<std::string>& tokens);
    void cmd_del(const std::vector<std::string>& tokens);
    void cmd_flush();
    void cmd_compact();
    void cmd_snapshot();
    void cmd_get_at(const std::vector<std::string>& tokens);
    void cmd_release(const std::vector<std::string>& tokens);
    void cmd_scan(const std::vector<std::string>& tokens);
    void cmd_prefix_scan(const std::vector<std::string>& tokens);
    void cmd_concurrent_test(const std::vector<std::string>& tokens);
    void cmd_benchmark(const std::vector<std::string>& tokens);
    void cmd_set_compaction_strategy(const std::vector<std::string>& tokens);
    void cmd_start_network(const std::vector<std::string>& tokens);
    void cmd_stop_network(const std::vector<std::string>& tokens);
    void cmd_stats();
    void cmd_lsm();
    void cmd_help();
    void cmd_man(const std::vector<std::string>& tokens);
    
    // 帮助系统相关
    bool is_help_request(const std::vector<std::string>& tokens);
    void show_command_help(const std::string& command);
    void show_man_page(const std::string& command);
};
