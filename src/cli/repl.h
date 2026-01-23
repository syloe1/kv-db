#pragma once
#include "db/kv_db.h"
#include "query/query_engine.h"
#ifdef ENABLE_NETWORK
#include "network/network_server.h"
#endif
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <fstream>

class REPL {
public:
    explicit REPL(KVDB& db);
    ~REPL();
    void run();

private:
    KVDB& db_;
    std::unique_ptr<QueryEngine> query_engine_;
#ifdef ENABLE_NETWORK
    std::unique_ptr<NetworkServer> network_server_;  // 网络服务器
#endif
    std::vector<uint64_t> active_snapshots_; // 存储 snapshot seq，用于管理
    
    // 多行输入支持
    bool multi_line_mode_;
    std::string multi_line_buffer_;
    std::string multi_line_prompt_;
    
    // 脚本执行支持
    bool script_mode_;
    std::ifstream script_file_;
    
    // 语法高亮支持
    bool syntax_highlighting_;
    std::unordered_map<std::string, std::string> color_map_;
    
    std::vector<std::string> split(const std::string& line);
    void execute_command(const std::vector<std::string>& tokens);
    
    // readline 相关
    char* read_input(const char* prompt);
    void setup_readline();
    
    // 命令补全相关（readline 函数指针类型）
    static char** command_completion(const char* text, int start, int end);
    static char* command_generator(const char* text, int state);
    static char* parameter_generator(const char* text, int state);
    static std::vector<std::string> commands_;
    static std::vector<std::string> current_parameters_;
    
    // 语法高亮相关
    void setup_colors();
    std::string apply_syntax_highlighting(const std::string& line);
    std::string colorize_command(const std::string& command);
    std::string colorize_parameter(const std::string& param, const std::string& command);
    void print_colored_line(const std::string& line);
    
    // 多行输入相关
    bool is_multi_line_command(const std::string& line);
    bool is_multi_line_complete(const std::string& buffer);
    void enter_multi_line_mode(const std::string& initial_line);
    void exit_multi_line_mode();
    std::string process_multi_line_input();
    
    // 脚本执行相关
    bool execute_script(const std::string& filename);
    void execute_script_line(const std::string& line);
    bool is_comment_line(const std::string& line);
    std::string trim_line(const std::string& line);
    
    // 增强的输入处理
    std::string read_line_with_highlighting(const char* prompt);
    void update_parameter_completion(const std::vector<std::string>& tokens);
    
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
    
    // 新增命令
    void cmd_source(const std::vector<std::string>& tokens);  // 执行脚本文件
    void cmd_multiline(const std::vector<std::string>& tokens);  // 多行模式控制
    void cmd_highlight(const std::vector<std::string>& tokens);  // 语法高亮控制
    void cmd_history(const std::vector<std::string>& tokens);   // 历史记录管理
    void cmd_clear();  // 清屏
    void cmd_echo(const std::vector<std::string>& tokens);  // 回显命令
    
    // 新增高级查询命令
    void cmd_batch_put(const std::vector<std::string>& tokens);
    void cmd_batch_get(const std::vector<std::string>& tokens);
    void cmd_batch_del(const std::vector<std::string>& tokens);
    void cmd_get_where(const std::vector<std::string>& tokens);
    void cmd_count(const std::vector<std::string>& tokens);
    void cmd_sum(const std::vector<std::string>& tokens);
    void cmd_avg(const std::vector<std::string>& tokens);
    void cmd_min_max(const std::vector<std::string>& tokens);
    void cmd_scan_ordered(const std::vector<std::string>& tokens);
    
    // 帮助系统相关
    bool is_help_request(const std::vector<std::string>& tokens);
    void show_command_help(const std::string& command);
    void show_man_page(const std::string& command);
    
    // 颜色常量
    static const std::string RESET;
    static const std::string RED;
    static const std::string GREEN;
    static const std::string YELLOW;
    static const std::string BLUE;
    static const std::string MAGENTA;
    static const std::string CYAN;
    static const std::string WHITE;
    static const std::string BOLD;
    static const std::string DIM;
};
