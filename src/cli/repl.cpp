#include "cli/repl.h"
#include "query/query_engine.h"
#include "iterator/iterator.h"
#include "benchmark/ycsb_benchmark.h"
#ifdef ENABLE_NETWORK
#include "network/network_server.h"
#endif
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <fstream>

// readline 头文件
#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

// 静态成员初始化
std::vector<std::string> REPL::commands_ = {
    "PUT", "GET", "DEL", "FLUSH", "COMPACT", 
    "SNAPSHOT", "GET_AT", "RELEASE", "SCAN", "PREFIX_SCAN",
    "CONCURRENT_TEST", "BENCHMARK", "SET_COMPACTION", 
    "START_NETWORK", "STOP_NETWORK", "STATS", "LSM", "HELP", "MAN", 
    "SOURCE", "MULTILINE", "HIGHLIGHT", "HISTORY", "CLEAR", "ECHO",
    // 新增高级查询命令
    "BATCH", "GET_WHERE", "COUNT", "SUM", "AVG", "MIN_MAX", "SCAN_ORDER",
    "EXIT", "QUIT"
};

std::vector<std::string> REPL::current_parameters_;

// 颜色常量定义
const std::string REPL::RESET = "\033[0m";
const std::string REPL::RED = "\033[31m";
const std::string REPL::GREEN = "\033[32m";
const std::string REPL::YELLOW = "\033[33m";
const std::string REPL::BLUE = "\033[34m";
const std::string REPL::MAGENTA = "\033[35m";
const std::string REPL::CYAN = "\033[36m";
const std::string REPL::WHITE = "\033[37m";
const std::string REPL::BOLD = "\033[1m";
const std::string REPL::DIM = "\033[2m";

REPL::REPL(KVDB& db) : db_(db), multi_line_mode_(false), script_mode_(false), syntax_highlighting_(true) {
    setup_readline();
    setup_colors();
    query_engine_ = std::make_unique<QueryEngine>(db_);
#ifdef ENABLE_NETWORK
    network_server_ = std::make_unique<NetworkServer>(db_);
#endif
}

REPL::~REPL() {
#ifdef HAVE_READLINE
    // 保存历史到文件
    write_history(".kvdb_history");
    // 清理 readline 历史
    clear_history();
#endif
}

std::vector<std::string> REPL::split(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

void REPL::setup_colors() {
    color_map_["PUT"] = GREEN + BOLD;
    color_map_["GET"] = BLUE + BOLD;
    color_map_["DEL"] = RED + BOLD;
    color_map_["FLUSH"] = YELLOW + BOLD;
    color_map_["COMPACT"] = YELLOW + BOLD;
    color_map_["SNAPSHOT"] = MAGENTA + BOLD;
    color_map_["GET_AT"] = BLUE;
    color_map_["RELEASE"] = MAGENTA;
    color_map_["SCAN"] = CYAN + BOLD;
    color_map_["PREFIX_SCAN"] = CYAN + BOLD;
    color_map_["CONCURRENT_TEST"] = YELLOW;
    color_map_["BENCHMARK"] = RED;
    color_map_["SET_COMPACTION"] = YELLOW;
    color_map_["START_NETWORK"] = GREEN;
    color_map_["STOP_NETWORK"] = RED;
    color_map_["STATS"] = WHITE + BOLD;
    color_map_["LSM"] = WHITE + BOLD;
    color_map_["HELP"] = CYAN;
    color_map_["MAN"] = CYAN;
    color_map_["SOURCE"] = MAGENTA;
    color_map_["MULTILINE"] = YELLOW;
    color_map_["HIGHLIGHT"] = YELLOW;
    color_map_["HISTORY"] = WHITE;
    color_map_["CLEAR"] = WHITE;
    color_map_["ECHO"] = GREEN;
    color_map_["EXIT"] = RED + BOLD;
    color_map_["QUIT"] = RED + BOLD;
    
    // 新增高级查询命令颜色
    color_map_["BATCH"] = MAGENTA + BOLD;
    color_map_["GET_WHERE"] = BLUE + BOLD;
    color_map_["COUNT"] = CYAN + BOLD;
    color_map_["SUM"] = CYAN + BOLD;
    color_map_["AVG"] = CYAN + BOLD;
    color_map_["MIN_MAX"] = CYAN + BOLD;
    color_map_["SCAN_ORDER"] = CYAN + BOLD;
}

std::string REPL::apply_syntax_highlighting(const std::string& line) {
    if (!syntax_highlighting_) {
        return line;
    }
    
    auto tokens = split(line);
    if (tokens.empty()) {
        return line;
    }
    
    std::string result;
    std::string command = tokens[0];
    
    // 转换为大写进行匹配
    std::string upper_command = command;
    for (char& c : upper_command) {
        c = std::toupper(c);
    }
    
    // 高亮命令
    result += colorize_command(upper_command);
    
    // 高亮参数
    for (size_t i = 1; i < tokens.size(); i++) {
        result += " " + colorize_parameter(tokens[i], upper_command);
    }
    
    result += RESET;
    return result;
}

std::string REPL::colorize_command(const std::string& command) {
    auto it = color_map_.find(command);
    if (it != color_map_.end()) {
        return it->second + command + RESET;
    }
    return RED + command + RESET;  // 未知命令用红色
}

std::string REPL::colorize_parameter(const std::string& param, const std::string& command) {
    // 根据命令类型对参数进行不同的着色
    if (command == "PUT" || command == "GET" || command == "DEL") {
        return CYAN + param + RESET;  // 键值参数用青色
    } else if (command == "SCAN" || command == "PREFIX_SCAN") {
        return YELLOW + param + RESET;  // 扫描参数用黄色
    } else if (command == "BENCHMARK") {
        return MAGENTA + param + RESET;  // 基准测试参数用紫色
    } else if (param == "HELP" || param == "-" || param == "help") {
        return GREEN + param + RESET;  // 帮助参数用绿色
    }
    return WHITE + param + RESET;  // 默认参数用白色
}

void REPL::print_colored_line(const std::string& line) {
    std::cout << apply_syntax_highlighting(line) << std::endl;
}

void REPL::setup_readline() {
#ifdef HAVE_READLINE
    // 设置补全函数
    rl_attempted_completion_function = command_completion;
    
    // 设置历史文件
    const char* histfile = ".kvdb_history";
    read_history(histfile);
    
    // 设置最大历史记录数
    stifle_history(1000);
    
    // 保存历史文件的钩子
    rl_bind_key('\t', rl_complete);  // Tab 键补全
#endif
}

bool REPL::is_multi_line_command(const std::string& line) {
    // 检查是否是多行命令的开始
    std::string trimmed = trim_line(line);
    return trimmed.back() == '\\' || trimmed == "MULTILINE";
}

bool REPL::is_multi_line_complete(const std::string& buffer) {
    // 检查多行输入是否完成
    std::string trimmed = trim_line(buffer);
    return !trimmed.empty() && trimmed.back() != '\\';
}

void REPL::enter_multi_line_mode(const std::string& initial_line) {
    multi_line_mode_ = true;
    multi_line_buffer_ = initial_line;
    multi_line_prompt_ = "... ";
    
    if (syntax_highlighting_) {
        std::cout << YELLOW << "Entering multi-line mode. End with a line not ending in '\\' or type 'END'" << RESET << std::endl;
    } else {
        std::cout << "Entering multi-line mode. End with a line not ending in '\\' or type 'END'" << std::endl;
    }
}

void REPL::exit_multi_line_mode() {
    multi_line_mode_ = false;
    multi_line_buffer_.clear();
    multi_line_prompt_.clear();
}

std::string REPL::process_multi_line_input() {
    std::string result = multi_line_buffer_;
    
    // 移除行尾的反斜杠并连接行
    std::string processed;
    std::istringstream iss(result);
    std::string line;
    
    while (std::getline(iss, line)) {
        std::string trimmed = trim_line(line);
        if (!trimmed.empty() && trimmed.back() == '\\') {
            trimmed.pop_back();  // 移除反斜杠
            processed += trimmed + " ";
        } else {
            processed += trimmed;
        }
    }
    
    return processed;
}

bool REPL::execute_script(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "Error: Cannot open script file '" << filename << "'" << std::endl;
        return false;
    }
    
    script_mode_ = true;
    std::string line;
    int line_number = 0;
    
    std::cout << "Executing script: " << filename << std::endl;
    
    while (std::getline(file, line)) {
        line_number++;
        
        if (is_comment_line(line)) {
            continue;  // 跳过注释行
        }
        
        std::string trimmed = trim_line(line);
        if (trimmed.empty()) {
            continue;  // 跳过空行
        }
        
        try {
            if (syntax_highlighting_) {
                std::cout << DIM << "[" << line_number << "] " << RESET;
                print_colored_line(trimmed);
            } else {
                std::cout << "[" << line_number << "] " << trimmed << std::endl;
            }
            
            execute_script_line(trimmed);
            
        } catch (const std::exception& e) {
            std::cout << RED << "Error at line " << line_number << ": " << e.what() << RESET << std::endl;
            script_mode_ = false;
            return false;
        }
    }
    
    script_mode_ = false;
    std::cout << "Script execution completed." << std::endl;
    return true;
}

void REPL::execute_script_line(const std::string& line) {
    auto tokens = split(line);
    if (tokens.empty()) return;
    
    // 转换为大写
    std::string cmd = tokens[0];
    for (char& c : cmd) {
        c = std::toupper(c);
    }
    tokens[0] = cmd;
    
    execute_command(tokens);
}

bool REPL::is_comment_line(const std::string& line) {
    std::string trimmed = trim_line(line);
    return trimmed.empty() || trimmed[0] == '#' || trimmed.substr(0, 2) == "//";
}

std::string REPL::trim_line(const std::string& line) {
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    
    size_t end = line.find_last_not_of(" \t\r\n");
    return line.substr(start, end - start + 1);
}

std::string REPL::read_line_with_highlighting(const char* prompt) {
    char* line_cstr = read_input(prompt);
    if (!line_cstr) {
        return "";
    }
    
    std::string line(line_cstr);
    free(line_cstr);
    
    return line;
}

void REPL::update_parameter_completion(const std::vector<std::string>& tokens) {
    current_parameters_.clear();
    
    if (tokens.empty()) return;
    
    std::string command = tokens[0];
    for (char& c : command) {
        c = std::toupper(c);
    }
    
    // 根据命令提供参数补全
    if (command == "SET_COMPACTION") {
        current_parameters_ = {"LEVELED", "TIERED", "SIZE_TIERED", "TIME_WINDOW"};
    } else if (command == "START_NETWORK" || command == "STOP_NETWORK") {
        current_parameters_ = {"grpc", "websocket", "all"};
    } else if (command == "BENCHMARK") {
        current_parameters_ = {"A", "B", "C", "D", "E", "F"};
    } else if (command == "MULTILINE") {
        current_parameters_ = {"ON", "OFF", "STATUS"};
    } else if (command == "HIGHLIGHT") {
        current_parameters_ = {"ON", "OFF", "STATUS"};
    } else if (command == "HISTORY") {
        current_parameters_ = {"SHOW", "CLEAR", "SAVE", "LOAD"};
    }
}

char* REPL::read_input(const char* prompt) {
#ifdef HAVE_READLINE
    char* line = readline(prompt);
    if (line && *line) {
        add_history(line);  // 添加到历史记录
    }
    return line;
#else
    std::cout << prompt;
    std::string line;
    if (std::getline(std::cin, line)) {
        char* result = (char*)malloc(line.length() + 1);
        strcpy(result, line.c_str());
        return result;
    }
    return nullptr;
#endif
}

void REPL::run() {
    if (syntax_highlighting_) {
        std::cout << BOLD << CYAN << "KVDB v1.0.0" << RESET << " - Enhanced Command Line Interface\n";
        std::cout << GREEN << "Features:" << RESET << " ↑/↓ history, TAB completion, syntax highlighting, multi-line input, script support\n";
        std::cout << "Type " << YELLOW << BOLD << "HELP" << RESET << " for commands, " 
                  << YELLOW << BOLD << "MAN <command>" << RESET << " for detailed help\n\n";
    } else {
        std::cout << "KVDB v1.0.0 - Enhanced Command Line Interface\n";
        std::cout << "Features: ↑/↓ history, TAB completion, multi-line input, script support\n";
        std::cout << "Type 'HELP' for commands, 'MAN <command>' for detailed help\n\n";
    }
    
    while (true) {
        std::string prompt = multi_line_mode_ ? multi_line_prompt_ : "kvdb> ";
        
        if (syntax_highlighting_ && !multi_line_mode_) {
            prompt = BOLD + BLUE + "kvdb" + RESET + "> ";
        }
        
        char* line_cstr = read_input(prompt.c_str());
        if (!line_cstr) {
            std::cout << "\nBye!\n";
            break;
        }
        
        std::string line(line_cstr);
        free(line_cstr);
        
        if (line.empty()) continue;
        
        // 处理多行输入
        if (multi_line_mode_) {
            if (trim_line(line) == "END") {
                // 结束多行输入
                std::string complete_command = process_multi_line_input();
                exit_multi_line_mode();
                
                if (!complete_command.empty()) {
                    auto tokens = split(complete_command);
                    if (!tokens.empty()) {
                        // 转换为大写
                        std::string cmd = tokens[0];
                        for (char& c : cmd) {
                            c = std::toupper(c);
                        }
                        tokens[0] = cmd;
                        
                        if (cmd == "EXIT" || cmd == "QUIT") {
                            std::cout << "Bye!\n";
                            break;
                        }
                        
                        execute_command(tokens);
                    }
                }
                continue;
            } else {
                // 继续多行输入
                multi_line_buffer_ += "\n" + line;
                if (is_multi_line_complete(line)) {
                    // 多行输入完成
                    std::string complete_command = process_multi_line_input();
                    exit_multi_line_mode();
                    
                    if (!complete_command.empty()) {
                        auto tokens = split(complete_command);
                        if (!tokens.empty()) {
                            // 转换为大写
                            std::string cmd = tokens[0];
                            for (char& c : cmd) {
                                c = std::toupper(c);
                            }
                            tokens[0] = cmd;
                            
                            if (cmd == "EXIT" || cmd == "QUIT") {
                                std::cout << "Bye!\n";
                                break;
                            }
                            
                            execute_command(tokens);
                        }
                    }
                }
                continue;
            }
        }
        
        // 检查是否开始多行输入
        if (is_multi_line_command(line)) {
            enter_multi_line_mode(line);
            continue;
        }
        
        auto tokens = split(line);
        if (tokens.empty()) continue;
        
        // 转换为大写
        std::string cmd = tokens[0];
        for (char& c : cmd) {
            c = std::toupper(c);
        }
        tokens[0] = cmd;
        
        if (cmd == "EXIT" || cmd == "QUIT") {
            std::cout << "Bye!\n";
            break;
        }
        
        execute_command(tokens);
    }
}

// readline 补全函数
#ifdef HAVE_READLINE
char** REPL::command_completion(const char* text, int start, int end) {
    (void)end;  // 未使用
    
    // 只在命令位置补全
    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    } else {
        // 参数位置补全
        return rl_completion_matches(text, parameter_generator);
    }
}

char* REPL::command_generator(const char* text, int state) {
    static int index = 0;
    static size_t len = 0;
    
    if (!state) {
        index = 0;
        len = strlen(text);
    }
    
    while (index < (int)commands_.size()) {
        const std::string& cmd = commands_[index++];
        if (cmd.length() >= len && cmd.substr(0, len) == text) {
            char* match = (char*)malloc(cmd.length() + 1);
            strcpy(match, cmd.c_str());
            return match;
        }
    }
    
    return nullptr;
}

char* REPL::parameter_generator(const char* text, int state) {
    static int index = 0;
    static size_t len = 0;
    
    if (!state) {
        index = 0;
        len = strlen(text);
    }
    
    while (index < (int)current_parameters_.size()) {
        const std::string& param = current_parameters_[index++];
        if (param.length() >= len && param.substr(0, len) == text) {
            char* match = (char*)malloc(param.length() + 1);
            strcpy(match, param.c_str());
            return match;
        }
    }
    
    return nullptr;
}
#endif

void REPL::execute_command(const std::vector<std::string>& tokens) {
    std::string cmd = tokens[0];
    
    // 更新参数补全
    update_parameter_completion(tokens);
    
    // 检查是否是帮助请求
    if (is_help_request(tokens)) {
        show_command_help(cmd);
        return;
    }
    
    try {
        if (cmd == "PUT") {
            cmd_put(tokens);
        } else if (cmd == "GET") {
            cmd_get(tokens);
        } else if (cmd == "DEL") {
            cmd_del(tokens);
        } else if (cmd == "FLUSH") {
            cmd_flush();
        } else if (cmd == "COMPACT") {
            cmd_compact();
        } else if (cmd == "SNAPSHOT") {
            cmd_snapshot();
        } else if (cmd == "GET_AT") {
            cmd_get_at(tokens);
        } else if (cmd == "RELEASE") {
            cmd_release(tokens);
        } else if (cmd == "SCAN") {
            cmd_scan(tokens);
        } else if (cmd == "PREFIX_SCAN") {
            cmd_prefix_scan(tokens);
        } else if (cmd == "CONCURRENT_TEST") {
            cmd_concurrent_test(tokens);
        } else if (cmd == "BENCHMARK") {
            cmd_benchmark(tokens);
        } else if (cmd == "SET_COMPACTION") {
            cmd_set_compaction_strategy(tokens);
        } else if (cmd == "START_NETWORK") {
            cmd_start_network(tokens);
        } else if (cmd == "STOP_NETWORK") {
            cmd_stop_network(tokens);
        } else if (cmd == "STATS") {
            cmd_stats();
        } else if (cmd == "LSM") {
            cmd_lsm();
        } else if (cmd == "HELP") {
            cmd_help();
        } else if (cmd == "MAN") {
            cmd_man(tokens);
        } else if (cmd == "SOURCE") {
            cmd_source(tokens);
        } else if (cmd == "MULTILINE") {
            cmd_multiline(tokens);
        } else if (cmd == "HIGHLIGHT") {
            cmd_highlight(tokens);
        } else if (cmd == "HISTORY") {
            cmd_history(tokens);
        } else if (cmd == "CLEAR") {
            cmd_clear();
        } else if (cmd == "ECHO") {
            cmd_echo(tokens);
        } else if (cmd == "BATCH") {
            // 批量操作：BATCH PUT key1 val1 key2 val2 ... 或 BATCH GET key1 key2 ... 或 BATCH DEL key1 key2 ...
            if (tokens.size() >= 2) {
                std::string sub_cmd = tokens[1];
                for (char& c : sub_cmd) {
                    c = std::toupper(c);
                }
                
                if (sub_cmd == "PUT") {
                    cmd_batch_put(tokens);
                } else if (sub_cmd == "GET") {
                    cmd_batch_get(tokens);
                } else if (sub_cmd == "DEL") {
                    cmd_batch_del(tokens);
                } else {
                    std::cout << "Usage: BATCH <PUT|GET|DEL> <args...>\n";
                }
            } else {
                std::cout << "Usage: BATCH <PUT|GET|DEL> <args...>\n";
            }
        } else if (cmd == "GET_WHERE") {
            cmd_get_where(tokens);
        } else if (cmd == "COUNT") {
            cmd_count(tokens);
        } else if (cmd == "SUM") {
            cmd_sum(tokens);
        } else if (cmd == "AVG") {
            cmd_avg(tokens);
        } else if (cmd == "MIN_MAX") {
            cmd_min_max(tokens);
        } else if (cmd == "SCAN_ORDER") {
            cmd_scan_ordered(tokens);
        } else {
            if (syntax_highlighting_) {
                std::cout << RED << "Unknown command: " << cmd << RESET << ". Type " 
                          << YELLOW << BOLD << "HELP" << RESET << " for help.\n";
            } else {
                std::cout << "Unknown command: " << cmd << ". Type 'HELP' for help.\n";
            }
        }
    } catch (const std::exception& e) {
        if (syntax_highlighting_) {
            std::cout << RED << "Error: " << e.what() << RESET << "\n";
        } else {
            std::cout << "Error: " << e.what() << "\n";
        }
    }
}

void REPL::cmd_put(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        std::cout << "Usage: PUT <key> <value>\n";
        return;
    }
    
    db_.put(tokens[1], tokens[2]);
    std::cout << "OK\n";
}

void REPL::cmd_get(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: GET <key>\n";
        return;
    }
    
    std::string value;
    if (db_.get(tokens[1], value)) {
        std::cout << value << "\n";
    } else {
        std::cout << "NOT FOUND\n";
    }
}

void REPL::cmd_del(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: DEL <key>\n";
        return;
    }
    
    db_.del(tokens[1]);
    std::cout << "OK\n";
}

void REPL::cmd_flush() {
    db_.flush();
    std::cout << "OK\n";
}

void REPL::cmd_compact() {
    db_.compact();
    std::cout << "OK\n";
}

void REPL::cmd_snapshot() {
    Snapshot snap = db_.get_snapshot();
    active_snapshots_.push_back(snap.seq);
    std::cout << "snapshot_id = " << snap.seq << "\n";
}

void REPL::cmd_get_at(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        std::cout << "Usage: GET_AT <key> <snapshot_id>\n";
        return;
    }
    
    uint64_t snapshot_id = std::stoull(tokens[2]);
    Snapshot snap(snapshot_id);
    
    std::string value;
    if (db_.get(tokens[1], snap, value)) {
        std::cout << value << "\n";
    } else {
        std::cout << "NOT FOUND\n";
    }
}

void REPL::cmd_release(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: RELEASE <snapshot_id>\n";
        return;
    }
    
    uint64_t snapshot_id = std::stoull(tokens[1]);
    auto it = std::find(active_snapshots_.begin(), active_snapshots_.end(), snapshot_id);
    if (it != active_snapshots_.end()) {
        Snapshot snap(snapshot_id);
        db_.release_snapshot(snap);
        active_snapshots_.erase(it);
        std::cout << "OK\n";
    } else {
        std::cout << "Snapshot " << snapshot_id << " not found\n";
    }
}

void REPL::cmd_scan(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        std::cout << "Usage: SCAN <begin_key> <end_key>\n";
        return;
    }
    
    Snapshot snap = db_.get_snapshot();
    auto it = db_.new_iterator(snap);
    
    int count = 0;
    for (it->seek(tokens[1]); it->valid() && it->key() <= tokens[2]; it->next()) {
        std::string value = it->value();
        if (!value.empty()) {
            std::cout << it->key() << " = " << value << "\n";
            count++;
        }
    }
    std::cout << "Found " << count << " key(s)\n";
}

void REPL::cmd_prefix_scan(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: PREFIX_SCAN <prefix>\n";
        return;
    }
    
    Snapshot snap = db_.get_snapshot();
    auto it = db_.new_prefix_iterator(snap, tokens[1]);
    
    int count = 0;
    while (it->valid()) {
        std::string key = it->key();
        std::string value = it->value();
        
        // 检查是否还在前缀范围内
        if (!(key.size() >= tokens[1].size() && 
              key.substr(0, tokens[1].size()) == tokens[1])) {
            break;
        }
        
        if (!value.empty()) {
            std::cout << key << " = " << value << "\n";
            count++;
        }
        it->next();
    }
    std::cout << "Found " << count << " key(s) with prefix '" << tokens[1] << "'\n";
}

void REPL::cmd_concurrent_test(const std::vector<std::string>& tokens) {
    std::cout << "=== 并发迭代器测试 ===\n";
    
    // 插入测试数据
    for (int i = 1; i <= 10; i++) {
        db_.put("test:" + std::to_string(i), "value_" + std::to_string(i));
    }
    
    Snapshot snap = db_.get_snapshot();
    
    // 创建并发迭代器
    auto iter1 = db_.new_concurrent_iterator(snap);
    auto iter2 = db_.new_concurrent_prefix_iterator(snap, "test:");
    
    std::cout << "创建了2个并发迭代器\n";
    
    // 测试读操作
    std::cout << "\n--- 迭代器1 (全扫描) ---\n";
    int count1 = 0;
    for (iter1->seek_to_first(); iter1->valid(); iter1->next()) {
        if (iter1->key().find("test:") == 0) {
            std::cout << iter1->key() << " = " << iter1->value() << "\n";
            count1++;
        }
    }
    std::cout << "迭代器1 找到 " << count1 << " 个记录\n";
    
    std::cout << "\n--- 迭代器2 (前缀扫描) ---\n";
    int count2 = 0;
    while (iter2->valid()) {
        std::cout << iter2->key() << " = " << iter2->value() << "\n";
        iter2->next();
        count2++;
    }
    std::cout << "迭代器2 找到 " << count2 << " 个记录\n";
    
    // 测试写操作对迭代器的影响
    std::cout << "\n--- 执行写操作 ---\n";
    db_.put("test:11", "value_11");
    db_.del("test:5");
    
    std::cout << "写操作完成，迭代器应该已失效\n";
    
    // 尝试使用失效的迭代器
    std::cout << "\n--- 测试失效的迭代器 ---\n";
    std::cout << "迭代器1 有效性: " << (iter1->valid() ? "有效" : "无效") << "\n";
    std::cout << "迭代器2 有效性: " << (iter2->valid() ? "有效" : "无效") << "\n";
    
    // 创建新的迭代器验证数据
    std::cout << "\n--- 新迭代器验证数据 ---\n";
    Snapshot new_snap = db_.get_snapshot();
    auto new_iter = db_.new_concurrent_prefix_iterator(new_snap, "test:");
    
    int new_count = 0;
    while (new_iter->valid()) {
        std::cout << new_iter->key() << " = " << new_iter->value() << "\n";
        new_iter->next();
        new_count++;
    }
    std::cout << "新迭代器找到 " << new_count << " 个记录\n";
    
    std::cout << "\n=== 并发测试完成 ===\n";
}

void REPL::cmd_benchmark(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: BENCHMARK <workload> [records] [operations] [threads]\n";
        std::cout << "Workloads: A, B, C, D, E, F\n";
        std::cout << "Example: BENCHMARK A 1000 5000 4\n";
        return;
    }
    
    // 解析工作负载类型
    WorkloadType workload;
    std::string workload_str = tokens[1];
    if (workload_str == "A") {
        workload = WorkloadType::WORKLOAD_A;
    } else if (workload_str == "B") {
        workload = WorkloadType::WORKLOAD_B;
    } else if (workload_str == "C") {
        workload = WorkloadType::WORKLOAD_C;
    } else if (workload_str == "D") {
        workload = WorkloadType::WORKLOAD_D;
    } else if (workload_str == "E") {
        workload = WorkloadType::WORKLOAD_E;
    } else if (workload_str == "F") {
        workload = WorkloadType::WORKLOAD_F;
    } else {
        std::cout << "Invalid workload: " << workload_str << std::endl;
        return;
    }
    
    // 解析参数
    size_t records = (tokens.size() > 2) ? std::stoull(tokens[2]) : 1000;
    size_t operations = (tokens.size() > 3) ? std::stoull(tokens[3]) : 5000;
    size_t threads = (tokens.size() > 4) ? std::stoull(tokens[4]) : 1;
    
    // 创建并配置 benchmark
    YCSBBenchmark benchmark(db_);
    benchmark.set_workload_type(workload);
    benchmark.set_record_count(records);
    benchmark.set_operation_count(operations);
    benchmark.set_thread_count(threads);
    
    // 预加载数据
    benchmark.load_data();
    
    // 运行 benchmark
    auto result = benchmark.run_benchmark();
    
    // 打印结果
    benchmark.print_results(result);
}

void REPL::cmd_set_compaction_strategy(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: SET_COMPACTION <strategy>\n";
        std::cout << "Available strategies:\n";
        std::cout << "  LEVELED     - Leveled compaction (default, reduces read amplification)\n";
        std::cout << "  TIERED      - Tiered compaction (reduces write amplification)\n";
        std::cout << "  SIZE_TIERED - Size-tiered compaction (similar sizes merged)\n";
        std::cout << "  TIME_WINDOW - Time window compaction (for time-series data)\n";
        
        // 显示当前策略
        CompactionStrategyType current = db_.get_compaction_strategy();
        std::cout << "\nCurrent strategy: ";
        switch (current) {
            case CompactionStrategyType::LEVELED:
                std::cout << "LEVELED\n";
                break;
            case CompactionStrategyType::TIERED:
                std::cout << "TIERED\n";
                break;
            case CompactionStrategyType::SIZE_TIERED:
                std::cout << "SIZE_TIERED\n";
                break;
            case CompactionStrategyType::TIME_WINDOW:
                std::cout << "TIME_WINDOW\n";
                break;
        }
        return;
    }
    
    std::string strategy_str = tokens[1];
    CompactionStrategyType strategy;
    
    if (strategy_str == "LEVELED") {
        strategy = CompactionStrategyType::LEVELED;
    } else if (strategy_str == "TIERED") {
        strategy = CompactionStrategyType::TIERED;
    } else if (strategy_str == "SIZE_TIERED") {
        strategy = CompactionStrategyType::SIZE_TIERED;
    } else if (strategy_str == "TIME_WINDOW") {
        strategy = CompactionStrategyType::TIME_WINDOW;
    } else {
        std::cout << "Invalid strategy: " << strategy_str << std::endl;
        std::cout << "Available: LEVELED, TIERED, SIZE_TIERED, TIME_WINDOW\n";
        return;
    }
    
    db_.set_compaction_strategy(strategy);
    std::cout << "Compaction strategy set to: " << strategy_str << std::endl;
    
    // 显示策略统计信息
    const auto& stats = db_.get_compaction_stats();
    std::cout << "\nCompaction Statistics:\n";
    std::cout << "  Total compactions: " << stats.total_compactions << std::endl;
    std::cout << "  Bytes read: " << stats.bytes_read / (1024 * 1024) << " MB\n";
    std::cout << "  Bytes written: " << stats.bytes_written / (1024 * 1024) << " MB\n";
    std::cout << "  Write amplification: " << std::fixed << std::setprecision(2) 
              << stats.write_amplification << std::endl;
    std::cout << "  Total time: " << stats.total_time.count() << " ms\n";
}

void REPL::cmd_start_network(const std::vector<std::string>& tokens) {
#ifdef ENABLE_NETWORK
    if (tokens.size() < 2) {
        std::cout << "Usage: START_NETWORK <service> [options]\n";
        std::cout << "Services:\n";
        std::cout << "  grpc [address]     - Start gRPC server (default: 0.0.0.0:50051)\n";
        std::cout << "  websocket [port]   - Start WebSocket server (default: 8080)\n";
        std::cout << "  all                - Start all network services\n";
        return;
    }
    
    std::string service = tokens[1];
    
    if (service == "grpc") {
        std::string address = (tokens.size() > 2) ? tokens[2] : "0.0.0.0:50051";
        network_server_->start_grpc(address);
        std::cout << "gRPC server started on " << address << std::endl;
        
    } else if (service == "websocket") {
        uint16_t port = (tokens.size() > 2) ? static_cast<uint16_t>(std::stoi(tokens[2])) : 8080;
        network_server_->start_websocket(port);
        std::cout << "WebSocket server started on port " << port << std::endl;
        
    } else if (service == "all") {
        network_server_->start_all();
        std::cout << "All network services started" << std::endl;
        
    } else {
        std::cout << "Unknown service: " << service << std::endl;
        std::cout << "Available services: grpc, websocket, all\n";
    }
#else
    std::cout << "Network support not enabled. Recompile with ENABLE_NETWORK=ON\n";
#endif
}

void REPL::cmd_stop_network(const std::vector<std::string>& tokens) {
#ifdef ENABLE_NETWORK
    if (tokens.size() < 2) {
        std::cout << "Usage: STOP_NETWORK <service>\n";
        std::cout << "Services:\n";
        std::cout << "  grpc       - Stop gRPC server\n";
        std::cout << "  websocket  - Stop WebSocket server\n";
        std::cout << "  all        - Stop all network services\n";
        
        // 显示当前状态
        std::cout << "\nCurrent status:\n";
        std::cout << "  gRPC: " << (network_server_->is_grpc_running() ? "Running" : "Stopped") << "\n";
        std::cout << "  WebSocket: " << (network_server_->is_websocket_running() ? "Running" : "Stopped") << "\n";
        return;
    }
    
    std::string service = tokens[1];
    
    if (service == "grpc") {
        network_server_->stop_grpc();
        std::cout << "gRPC server stopped" << std::endl;
        
    } else if (service == "websocket") {
        network_server_->stop_websocket();
        std::cout << "WebSocket server stopped" << std::endl;
        
    } else if (service == "all") {
        network_server_->stop_all();
        std::cout << "All network services stopped" << std::endl;
        
    } else {
        std::cout << "Unknown service: " << service << std::endl;
        std::cout << "Available services: grpc, websocket, all\n";
    }
#else
    std::cout << "Network support not enabled. Recompile with ENABLE_NETWORK=ON\n";
#endif
}

void REPL::cmd_stats() {
    // 获取基本统计信息
    std::cout << "=== Database Statistics ===\n";
    std::cout << "MemTable: " << db_.get_memtable_size() << " bytes\n";
    std::cout << "WAL: " << db_.get_wal_size() << " bytes\n";
    std::cout << "Active Snapshots: " << active_snapshots_.size() << "\n";
    if (!active_snapshots_.empty()) {
        std::cout << "  Snapshot IDs: ";
        for (size_t i = 0; i < active_snapshots_.size(); i++) {
            std::cout << active_snapshots_[i];
            if (i < active_snapshots_.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
    }
    std::cout << "Cache Hit Rate: " << std::fixed << std::setprecision(2) 
              << db_.get_cache_hit_rate() << "%\n";
}

void REPL::cmd_lsm() {
    std::cout << "=== LSM Tree Structure ===\n";
    db_.print_lsm_structure();
}

void REPL::cmd_help() {
    if (syntax_highlighting_) {
        std::cout << BOLD << CYAN << "Available commands:" << RESET << "\n";
        std::cout << "  " << GREEN << BOLD << "PUT" << RESET << " <key> <value>          - Insert or update a key-value pair\n";
        std::cout << "  " << BLUE << BOLD << "GET" << RESET << " <key>                  - Get value for a key\n";
        std::cout << "  " << RED << BOLD << "DEL" << RESET << " <key>                  - Delete a key\n";
        std::cout << "  " << YELLOW << BOLD << "FLUSH" << RESET << "                      - Flush MemTable to disk\n";
        std::cout << "  " << YELLOW << BOLD << "COMPACT" << RESET << "                    - Trigger manual compaction\n";
        std::cout << "  " << MAGENTA << BOLD << "SNAPSHOT" << RESET << "                   - Create a snapshot\n";
        std::cout << "  " << BLUE << "GET_AT" << RESET << " <key> <snapshot_id> - Get value at a specific snapshot\n";
        std::cout << "  " << MAGENTA << "RELEASE" << RESET << " <snapshot_id>      - Release a snapshot\n";
        std::cout << "  " << CYAN << BOLD << "SCAN" << RESET << " <begin> <end>         - Range scan from begin to end\n";
        std::cout << "  " << CYAN << BOLD << "PREFIX_SCAN" << RESET << " <prefix>       - Scan all keys with given prefix (optimized)\n";
        std::cout << "  " << YELLOW << "CONCURRENT_TEST" << RESET << "            - Test concurrent iterator functionality\n";
        std::cout << "  " << RED << "BENCHMARK" << RESET << " <workload> [args] - Run YCSB benchmark (A/B/C/D/E/F)\n";
        std::cout << "  " << YELLOW << "SET_COMPACTION" << RESET << " <strategy>   - Set compaction strategy (LEVELED/TIERED/SIZE_TIERED/TIME_WINDOW)\n";
#ifdef ENABLE_NETWORK
        std::cout << "  " << GREEN << "START_NETWORK" << RESET << " <service>     - Start network services (grpc/websocket/all)\n";
        std::cout << "  " << RED << "STOP_NETWORK" << RESET << " <service>      - Stop network services (grpc/websocket/all)\n";
#endif
        std::cout << "  " << WHITE << BOLD << "STATS" << RESET << "                      - Show database statistics\n";
        std::cout << "  " << WHITE << BOLD << "LSM" << RESET << "                        - Show LSM tree structure\n";
        std::cout << "\n" << BOLD << MAGENTA << "Advanced Query Features:" << RESET << "\n";
        std::cout << "  " << MAGENTA << BOLD << "BATCH" << RESET << " <PUT|GET|DEL> <args> - Batch operations\n";
        std::cout << "  " << BLUE << BOLD << "GET_WHERE" << RESET << " <field> <op> <val> - Conditional queries\n";
        std::cout << "  " << CYAN << BOLD << "COUNT" << RESET << " [WHERE ...]          - Count records\n";
        std::cout << "  " << CYAN << BOLD << "SUM" << RESET << " [pattern]             - Sum numeric values\n";
        std::cout << "  " << CYAN << BOLD << "AVG" << RESET << " [pattern]             - Average numeric values\n";
        std::cout << "  " << CYAN << BOLD << "MIN_MAX" << RESET << " [pattern]          - Min/Max numeric values\n";
        std::cout << "  " << CYAN << BOLD << "SCAN_ORDER" << RESET << " <ASC|DESC> [...]  - Ordered range scan\n";
        std::cout << "\n" << BOLD << YELLOW << "Enhanced CLI Features:" << RESET << "\n";
        std::cout << "  " << MAGENTA << "SOURCE" << RESET << " <filename>         - Execute commands from script file\n";
        std::cout << "  " << YELLOW << "MULTILINE" << RESET << " <ON|OFF|STATUS>  - Control multi-line input mode\n";
        std::cout << "  " << YELLOW << "HIGHLIGHT" << RESET << " <ON|OFF|STATUS>  - Control syntax highlighting\n";
        std::cout << "  " << WHITE << "HISTORY" << RESET << " <SHOW|CLEAR|SAVE|LOAD> - Manage command history\n";
        std::cout << "  " << WHITE << "CLEAR" << RESET << "                      - Clear screen\n";
        std::cout << "  " << GREEN << "ECHO" << RESET << " <message>             - Display a message\n";
        std::cout << "  " << CYAN << "HELP" << RESET << "                       - Show this help message\n";
        std::cout << "  " << CYAN << "MAN" << RESET << " <command>              - Show detailed manual for a command\n";
        std::cout << "  " << RED << BOLD << "EXIT/QUIT" << RESET << "                  - Exit the database\n";
        std::cout << "\n" << BOLD << GREEN << "Tips:" << RESET << "\n";
        std::cout << "  • Use " << BOLD << "↑/↓" << RESET << " keys for command history\n";
        std::cout << "  • Use " << BOLD << "TAB" << RESET << " for command and parameter completion\n";
        std::cout << "  • End lines with " << BOLD << "\\" << RESET << " for multi-line commands\n";
        std::cout << "  • Use " << BOLD << "#" << RESET << " or " << BOLD << "//" << RESET << " for comments in scripts\n";
        std::cout << "\nFor detailed help: " << YELLOW << BOLD << "MAN <command>" << RESET << " or " << YELLOW << BOLD << "<command> HELP" << RESET << "\n";
    } else {
        std::cout << "Available commands:\n";
        std::cout << "  PUT <key> <value>          - Insert or update a key-value pair\n";
        std::cout << "  GET <key>                  - Get value for a key\n";
        std::cout << "  DEL <key>                  - Delete a key\n";
        std::cout << "  FLUSH                      - Flush MemTable to disk\n";
        std::cout << "  COMPACT                    - Trigger manual compaction\n";
        std::cout << "  SNAPSHOT                   - Create a snapshot\n";
        std::cout << "  GET_AT <key> <snapshot_id> - Get value at a specific snapshot\n";
        std::cout << "  RELEASE <snapshot_id>      - Release a snapshot\n";
        std::cout << "  SCAN <begin> <end>         - Range scan from begin to end\n";
        std::cout << "  PREFIX_SCAN <prefix>       - Scan all keys with given prefix (optimized)\n";
        std::cout << "  CONCURRENT_TEST            - Test concurrent iterator functionality\n";
        std::cout << "  BENCHMARK <workload> [args] - Run YCSB benchmark (A/B/C/D/E/F)\n";
        std::cout << "  SET_COMPACTION <strategy>   - Set compaction strategy (LEVELED/TIERED/SIZE_TIERED/TIME_WINDOW)\n";
#ifdef ENABLE_NETWORK
        std::cout << "  START_NETWORK <service>     - Start network services (grpc/websocket/all)\n";
        std::cout << "  STOP_NETWORK <service>      - Stop network services (grpc/websocket/all)\n";
#endif
        std::cout << "  STATS                      - Show database statistics\n";
        std::cout << "  LSM                        - Show LSM tree structure\n";
        std::cout << "\nAdvanced Query Features:\n";
        std::cout << "  BATCH <PUT|GET|DEL> <args> - Batch operations\n";
        std::cout << "  GET_WHERE <field> <op> <val> - Conditional queries\n";
        std::cout << "  COUNT [WHERE ...]          - Count records\n";
        std::cout << "  SUM [pattern]              - Sum numeric values\n";
        std::cout << "  AVG [pattern]              - Average numeric values\n";
        std::cout << "  MIN_MAX [pattern]          - Min/Max numeric values\n";
        std::cout << "  SCAN_ORDER <ASC|DESC> [...] - Ordered range scan\n";
        std::cout << "\nEnhanced CLI Features:\n";
        std::cout << "  SOURCE <filename>          - Execute commands from script file\n";
        std::cout << "  MULTILINE <ON|OFF|STATUS>  - Control multi-line input mode\n";
        std::cout << "  HIGHLIGHT <ON|OFF|STATUS>  - Control syntax highlighting\n";
        std::cout << "  HISTORY <SHOW|CLEAR|SAVE|LOAD> - Manage command history\n";
        std::cout << "  CLEAR                      - Clear screen\n";
        std::cout << "  ECHO <message>             - Display a message\n";
        std::cout << "  HELP                       - Show this help message\n";
        std::cout << "  MAN <command>              - Show detailed manual for a command\n";
        std::cout << "  EXIT/QUIT                  - Exit the database\n";
        std::cout << "\nTips:\n";
        std::cout << "  • Use ↑/↓ keys for command history\n";
        std::cout << "  • Use TAB for command and parameter completion\n";
        std::cout << "  • End lines with \\ for multi-line commands\n";
        std::cout << "  • Use # or // for comments in scripts\n";
        std::cout << "\nFor detailed help: MAN <command> or <command> HELP\n";
    }
}

void REPL::cmd_man(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: MAN <command>\n";
        std::cout << "Available commands: PUT, GET, DEL, FLUSH, COMPACT, SNAPSHOT, GET_AT, RELEASE, SCAN, PREFIX_SCAN, CONCURRENT_TEST, BENCHMARK, SET_COMPACTION, STATS, LSM, HELP\n";
        return;
    }
    
    std::string command = tokens[1];
    // 转换为大写
    for (char& c : command) {
        c = std::toupper(c);
    }
    
    show_man_page(command);
}

bool REPL::is_help_request(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) return false;
    
    // 检查 "COMMAND HELP" 格式
    if (tokens.size() == 2 && tokens[1] == "HELP") {
        return true;
    }
    
    // 检查 "COMMAND - HELP" 格式
    if (tokens.size() == 3 && tokens[1] == "-" && tokens[2] == "HELP") {
        return true;
    }
    
    return false;
}

void REPL::show_command_help(const std::string& command) {
    show_man_page(command);
}

void REPL::show_man_page(const std::string& command) {
    if (command == "PUT") {
        std::cout << "NAME\n";
        std::cout << "    PUT - Insert or update a key-value pair\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    PUT <key> <value>\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The PUT command inserts a new key-value pair into the database or\n";
        std::cout << "    updates an existing key with a new value. The operation is atomic\n";
        std::cout << "    and will be written to the Write-Ahead Log (WAL) before being\n";
        std::cout << "    stored in the MemTable.\n\n";
        std::cout << "PARAMETERS\n";
        std::cout << "    key     The key to insert or update (string)\n";
        std::cout << "    value   The value to associate with the key (string)\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    PUT user:1 \"John Doe\"\n";
        std::cout << "    PUT config:timeout 30\n";
        std::cout << "    PUT \"my key\" \"my value\"\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns \"OK\" on success\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    GET, DEL, FLUSH\n";
        
    } else if (command == "GET") {
        std::cout << "NAME\n";
        std::cout << "    GET - Retrieve value for a key\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    GET <key>\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The GET command retrieves the value associated with the specified\n";
        std::cout << "    key. It searches through the MemTable first, then through SSTable\n";
        std::cout << "    files in order from newest to oldest.\n\n";
        std::cout << "PARAMETERS\n";
        std::cout << "    key     The key to retrieve (string)\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    GET user:1\n";
        std::cout << "    GET config:timeout\n";
        std::cout << "    GET \"my key\"\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns the value if key exists, \"NOT FOUND\" otherwise\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    PUT, GET_AT, SCAN\n";
        
    } else if (command == "DEL") {
        std::cout << "NAME\n";
        std::cout << "    DEL - Delete a key\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    DEL <key>\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The DEL command marks a key as deleted by inserting a tombstone\n";
        std::cout << "    record. The key will not be returned by GET operations, but the\n";
        std::cout << "    actual space may not be reclaimed until compaction occurs.\n\n";
        std::cout << "PARAMETERS\n";
        std::cout << "    key     The key to delete (string)\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    DEL user:1\n";
        std::cout << "    DEL config:timeout\n";
        std::cout << "    DEL \"my key\"\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns \"OK\" on success (even if key doesn't exist)\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    PUT, GET, COMPACT\n";
        
    } else if (command == "FLUSH") {
        std::cout << "NAME\n";
        std::cout << "    FLUSH - Flush MemTable to disk\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    FLUSH\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The FLUSH command forces the current MemTable to be written to\n";
        std::cout << "    disk as an SSTable file. This operation is normally performed\n";
        std::cout << "    automatically when the MemTable reaches its size limit.\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    FLUSH\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns \"OK\" on success\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    COMPACT, STATS\n";
        
    } else if (command == "COMPACT") {
        std::cout << "NAME\n";
        std::cout << "    COMPACT - Trigger manual compaction\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    COMPACT\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The COMPACT command triggers a manual compaction process that\n";
        std::cout << "    merges SSTable files to reduce space usage and improve read\n";
        std::cout << "    performance. This removes deleted keys and consolidates data.\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    COMPACT\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns \"OK\" on success\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    FLUSH, LSM, STATS\n";
        
    } else if (command == "SNAPSHOT") {
        std::cout << "NAME\n";
        std::cout << "    SNAPSHOT - Create a database snapshot\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    SNAPSHOT\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The SNAPSHOT command creates a point-in-time snapshot of the\n";
        std::cout << "    database. This allows you to read data as it existed at the\n";
        std::cout << "    time the snapshot was created, even if the data is later\n";
        std::cout << "    modified or deleted.\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    SNAPSHOT\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns \"snapshot_id = <id>\" where <id> is the snapshot identifier\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    GET_AT, RELEASE\n";
        
    } else if (command == "GET_AT") {
        std::cout << "NAME\n";
        std::cout << "    GET_AT - Get value at a specific snapshot\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    GET_AT <key> <snapshot_id>\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The GET_AT command retrieves the value of a key as it existed\n";
        std::cout << "    at the time when the specified snapshot was created. This allows\n";
        std::cout << "    you to access historical versions of data.\n\n";
        std::cout << "PARAMETERS\n";
        std::cout << "    key         The key to retrieve (string)\n";
        std::cout << "    snapshot_id The snapshot identifier (number)\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    GET_AT user:1 12345\n";
        std::cout << "    GET_AT config:timeout 67890\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns the value if key existed at snapshot time, \"NOT FOUND\" otherwise\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    SNAPSHOT, RELEASE, GET\n";
        
    } else if (command == "RELEASE") {
        std::cout << "NAME\n";
        std::cout << "    RELEASE - Release a snapshot\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    RELEASE <snapshot_id>\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The RELEASE command releases a previously created snapshot,\n";
        std::cout << "    allowing the database to reclaim resources associated with\n";
        std::cout << "    maintaining that snapshot's view of the data.\n\n";
        std::cout << "PARAMETERS\n";
        std::cout << "    snapshot_id The snapshot identifier to release (number)\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    RELEASE 12345\n";
        std::cout << "    RELEASE 67890\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns \"OK\" on success, error message if snapshot not found\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    SNAPSHOT, GET_AT\n";
        
    } else if (command == "SCAN") {
        std::cout << "NAME\n";
        std::cout << "    SCAN - Range scan from begin to end key\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    SCAN <begin_key> <end_key>\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The SCAN command performs a range scan, returning all key-value\n";
        std::cout << "    pairs where the key is lexicographically between begin_key and\n";
        std::cout << "    end_key (inclusive). Results are returned in sorted order.\n\n";
        std::cout << "PARAMETERS\n";
        std::cout << "    begin_key   The starting key for the range (string)\n";
        std::cout << "    end_key     The ending key for the range (string)\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    SCAN user:1 user:9\n";
        std::cout << "    SCAN a z\n";
        std::cout << "    SCAN \"config:\" \"config:~\"\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns matching key-value pairs and a count summary\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    GET, SNAPSHOT\n";
        
    } else if (command == "PREFIX_SCAN") {
        std::cout << "NAME\n";
        std::cout << "    PREFIX_SCAN - Scan all keys with given prefix (optimized)\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    PREFIX_SCAN <prefix>\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The PREFIX_SCAN command performs an optimized prefix scan, returning\n";
        std::cout << "    all key-value pairs where the key starts with the specified prefix.\n";
        std::cout << "    This command uses heap-based merge iterator (O(log N)) and prefix\n";
        std::cout << "    filtering optimizations to improve performance compared to regular SCAN.\n\n";
        std::cout << "PARAMETERS\n";
        std::cout << "    prefix      The prefix to search for (string)\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    PREFIX_SCAN user:\n";
        std::cout << "    PREFIX_SCAN config:\n";
        std::cout << "    PREFIX_SCAN \"app:settings:\"\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns matching key-value pairs and a count summary\n\n";
        std::cout << "PERFORMANCE\n";
        std::cout << "    - Uses heap-based merge iterator for O(log N) complexity\n";
        std::cout << "    - Applies prefix filtering at iterator level\n";
        std::cout << "    - Skips non-matching SSTable blocks early\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    SCAN, GET\n";
        
    } else if (command == "CONCURRENT_TEST") {
        std::cout << "NAME\n";
        std::cout << "    CONCURRENT_TEST - Test concurrent iterator functionality\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    CONCURRENT_TEST\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The CONCURRENT_TEST command demonstrates the read-write isolation\n";
        std::cout << "    capabilities of concurrent iterators. It creates multiple iterators,\n";
        std::cout << "    performs read operations, then executes write operations to show\n";
        std::cout << "    how iterators are invalidated to maintain consistency.\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    CONCURRENT_TEST\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns test results and iterator validity status\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    SCAN, PREFIX_SCAN\n";
        
    } else if (command == "BENCHMARK") {
        std::cout << "NAME\n";
        std::cout << "    BENCHMARK - Run YCSB benchmark suite\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    BENCHMARK <workload> [records] [operations] [threads]\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The BENCHMARK command runs Yahoo! Cloud Serving Benchmark (YCSB)\n";
        std::cout << "    workloads to measure database performance. It supports all standard\n";
        std::cout << "    YCSB workloads with configurable parameters for comprehensive\n";
        std::cout << "    performance analysis.\n\n";
        std::cout << "PARAMETERS\n";
        std::cout << "    workload    YCSB workload type (A/B/C/D/E/F)\n";
        std::cout << "    records     Number of records to preload (default: 1000)\n";
        std::cout << "    operations  Number of operations to execute (default: 5000)\n";
        std::cout << "    threads     Number of concurrent threads (default: 1)\n\n";
        std::cout << "WORKLOAD TYPES\n";
        std::cout << "    A - 50% read, 50% update (heavy update)\n";
        std::cout << "    B - 95% read, 5% update (read mostly)\n";
        std::cout << "    C - 100% read (read only)\n";
        std::cout << "    D - 95% read, 5% insert (read latest)\n";
        std::cout << "    E - 95% scan, 5% insert (short ranges)\n";
        std::cout << "    F - 50% read, 50% read-modify-write\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    BENCHMARK A                    # Basic workload A\n";
        std::cout << "    BENCHMARK B 10000 50000 4     # Large scale test\n";
        std::cout << "    BENCHMARK C 1000 10000 1      # Read-only test\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns detailed performance metrics including throughput,\n";
        std::cout << "    latency percentiles, and operation breakdown\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    STATS, CONCURRENT_TEST\n";
        
    } else if (command == "SET_COMPACTION") {
        std::cout << "NAME\n";
        std::cout << "    SET_COMPACTION - Set compaction strategy\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    SET_COMPACTION <strategy>\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The SET_COMPACTION command changes the database's compaction strategy\n";
        std::cout << "    to optimize for different workload patterns. Each strategy has\n";
        std::cout << "    different trade-offs between read amplification, write amplification,\n";
        std::cout << "    and space amplification.\n\n";
        std::cout << "PARAMETERS\n";
        std::cout << "    strategy    Compaction strategy type\n\n";
        std::cout << "STRATEGIES\n";
        std::cout << "    LEVELED     - Leveled compaction (default)\n";
        std::cout << "                  + Reduces read amplification\n";
        std::cout << "                  + No overlapping keys in L1+\n";
        std::cout << "                  - Higher write amplification\n\n";
        std::cout << "    TIERED      - Tiered compaction\n";
        std::cout << "                  + Reduces write amplification\n";
        std::cout << "                  + Allows overlapping in all levels\n";
        std::cout << "                  - Higher read amplification\n\n";
        std::cout << "    SIZE_TIERED - Size-tiered compaction\n";
        std::cout << "                  + Merges similar-sized files\n";
        std::cout << "                  + Balanced read/write amplification\n";
        std::cout << "                  - Complex size management\n\n";
        std::cout << "    TIME_WINDOW - Time window compaction\n";
        std::cout << "                  + Optimized for time-series data\n";
        std::cout << "                  + Efficient time-based queries\n";
        std::cout << "                  - Requires time-ordered writes\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    SET_COMPACTION LEVELED      # For read-heavy workloads\n";
        std::cout << "    SET_COMPACTION TIERED       # For write-heavy workloads\n";
        std::cout << "    SET_COMPACTION TIME_WINDOW   # For time-series data\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns confirmation and current compaction statistics\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    COMPACT, STATS, LSM\n";
        
    } else if (command == "STATS") {
        std::cout << "NAME\n";
        std::cout << "    STATS - Show database statistics\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    STATS\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The STATS command displays various statistics about the database\n";
        std::cout << "    including MemTable size, WAL size, active snapshots, and cache\n";
        std::cout << "    hit rate. This information is useful for monitoring performance\n";
        std::cout << "    and resource usage.\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    STATS\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns formatted statistics information\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    LSM, FLUSH, COMPACT\n";
        
    } else if (command == "LSM") {
        std::cout << "NAME\n";
        std::cout << "    LSM - Show LSM tree structure\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    LSM\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The LSM command displays the current structure of the Log-Structured\n";
        std::cout << "    Merge Tree, showing information about different levels, SSTable\n";
        std::cout << "    files, and their organization. This is useful for understanding\n";
        std::cout << "    the internal state of the database.\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    LSM\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns formatted LSM tree structure information\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    STATS, COMPACT, FLUSH\n";
        
    } else if (command == "HELP") {
        std::cout << "NAME\n";
        std::cout << "    HELP - Show available commands\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    HELP\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The HELP command displays a summary of all available commands\n";
        std::cout << "    with brief descriptions. For detailed information about a\n";
        std::cout << "    specific command, use the MAN command.\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    HELP\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns list of available commands\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    MAN\n";
        
    } else if (command == "MAN") {
        std::cout << "NAME\n";
        std::cout << "    MAN - Show detailed manual for a command\n\n";
        std::cout << "SYNOPSIS\n";
        std::cout << "    MAN <command>\n\n";
        std::cout << "DESCRIPTION\n";
        std::cout << "    The MAN command displays detailed manual pages for database\n";
        std::cout << "    commands, including syntax, parameters, examples, and related\n";
        std::cout << "    commands. This provides comprehensive documentation for each\n";
        std::cout << "    available command.\n\n";
        std::cout << "PARAMETERS\n";
        std::cout << "    command     The command to show manual for (string)\n\n";
        std::cout << "EXAMPLES\n";
        std::cout << "    MAN PUT\n";
        std::cout << "    MAN SCAN\n";
        std::cout << "    MAN SNAPSHOT\n\n";
        std::cout << "RETURN VALUE\n";
        std::cout << "    Returns detailed manual page for the specified command\n\n";
        std::cout << "SEE ALSO\n";
        std::cout << "    HELP\n";
        
    } else {
        std::cout << "No manual entry for '" << command << "'\n";
        std::cout << "Available commands: PUT, GET, DEL, FLUSH, COMPACT, SNAPSHOT, GET_AT, RELEASE, SCAN, PREFIX_SCAN, CONCURRENT_TEST, BENCHMARK, SET_COMPACTION, STATS, LSM, HELP, MAN\n";
        std::cout << "Use 'HELP' to see all commands or 'MAN <command>' for specific help.\n";
    }
}

// 新增命令实现
void REPL::cmd_source(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: SOURCE <filename>\n";
        std::cout << "Execute commands from a script file\n";
        std::cout << "Example: SOURCE my_script.kvdb\n";
        return;
    }
    
    std::string filename = tokens[1];
    if (!execute_script(filename)) {
        if (syntax_highlighting_) {
            std::cout << RED << "Failed to execute script: " << filename << RESET << std::endl;
        } else {
            std::cout << "Failed to execute script: " << filename << std::endl;
        }
    }
}

void REPL::cmd_multiline(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: MULTILINE <ON|OFF|STATUS>\n";
        std::cout << "Control multi-line input mode\n";
        std::cout << "  ON     - Enable multi-line mode for next command\n";
        std::cout << "  OFF    - Disable multi-line mode\n";
        std::cout << "  STATUS - Show current multi-line status\n";
        return;
    }
    
    std::string action = tokens[1];
    for (char& c : action) {
        c = std::toupper(c);
    }
    
    if (action == "ON") {
        if (syntax_highlighting_) {
            std::cout << GREEN << "Multi-line mode enabled. End lines with '\\' to continue, or type 'END' to finish." << RESET << std::endl;
        } else {
            std::cout << "Multi-line mode enabled. End lines with '\\' to continue, or type 'END' to finish." << std::endl;
        }
        enter_multi_line_mode("");
    } else if (action == "OFF") {
        if (multi_line_mode_) {
            exit_multi_line_mode();
            if (syntax_highlighting_) {
                std::cout << YELLOW << "Multi-line mode disabled." << RESET << std::endl;
            } else {
                std::cout << "Multi-line mode disabled." << std::endl;
            }
        } else {
            std::cout << "Multi-line mode is already off." << std::endl;
        }
    } else if (action == "STATUS") {
        if (syntax_highlighting_) {
            std::cout << "Multi-line mode: " << (multi_line_mode_ ? GREEN + "ON" : RED + "OFF") << RESET << std::endl;
        } else {
            std::cout << "Multi-line mode: " << (multi_line_mode_ ? "ON" : "OFF") << std::endl;
        }
        if (multi_line_mode_) {
            std::cout << "Current buffer: " << multi_line_buffer_ << std::endl;
        }
    } else {
        std::cout << "Invalid action: " << action << std::endl;
        std::cout << "Valid actions: ON, OFF, STATUS" << std::endl;
    }
}

void REPL::cmd_highlight(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: HIGHLIGHT <ON|OFF|STATUS>\n";
        std::cout << "Control syntax highlighting\n";
        std::cout << "  ON     - Enable syntax highlighting\n";
        std::cout << "  OFF    - Disable syntax highlighting\n";
        std::cout << "  STATUS - Show current highlighting status\n";
        return;
    }
    
    std::string action = tokens[1];
    for (char& c : action) {
        c = std::toupper(c);
    }
    
    if (action == "ON") {
        syntax_highlighting_ = true;
        std::cout << GREEN << "Syntax highlighting enabled." << RESET << std::endl;
    } else if (action == "OFF") {
        syntax_highlighting_ = false;
        std::cout << "Syntax highlighting disabled." << std::endl;
    } else if (action == "STATUS") {
        if (syntax_highlighting_) {
            std::cout << "Syntax highlighting: " << GREEN << "ON" << RESET << std::endl;
        } else {
            std::cout << "Syntax highlighting: OFF" << std::endl;
        }
    } else {
        std::cout << "Invalid action: " << action << std::endl;
        std::cout << "Valid actions: ON, OFF, STATUS" << std::endl;
    }
}

void REPL::cmd_history(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: HISTORY <SHOW|CLEAR|SAVE|LOAD> [filename]\n";
        std::cout << "Manage command history\n";
        std::cout << "  SHOW        - Display command history\n";
        std::cout << "  CLEAR       - Clear command history\n";
        std::cout << "  SAVE [file] - Save history to file (default: .kvdb_history)\n";
        std::cout << "  LOAD [file] - Load history from file (default: .kvdb_history)\n";
        return;
    }
    
    std::string action = tokens[1];
    for (char& c : action) {
        c = std::toupper(c);
    }
    
#ifdef HAVE_READLINE
    if (action == "SHOW") {
        HIST_ENTRY** history_list = history_list_entries();
        if (history_list) {
            int count = 1;
            for (int i = 0; history_list[i]; i++) {
                if (syntax_highlighting_) {
                    std::cout << DIM << std::setw(4) << count++ << ": " << RESET 
                              << apply_syntax_highlighting(history_list[i]->line) << std::endl;
                } else {
                    std::cout << std::setw(4) << count++ << ": " << history_list[i]->line << std::endl;
                }
            }
        } else {
            std::cout << "No history available." << std::endl;
        }
    } else if (action == "CLEAR") {
        clear_history();
        if (syntax_highlighting_) {
            std::cout << YELLOW << "Command history cleared." << RESET << std::endl;
        } else {
            std::cout << "Command history cleared." << std::endl;
        }
    } else if (action == "SAVE") {
        std::string filename = (tokens.size() > 2) ? tokens[2] : ".kvdb_history";
        if (write_history(filename.c_str()) == 0) {
            if (syntax_highlighting_) {
                std::cout << GREEN << "History saved to " << filename << RESET << std::endl;
            } else {
                std::cout << "History saved to " << filename << std::endl;
            }
        } else {
            if (syntax_highlighting_) {
                std::cout << RED << "Failed to save history to " << filename << RESET << std::endl;
            } else {
                std::cout << "Failed to save history to " << filename << std::endl;
            }
        }
    } else if (action == "LOAD") {
        std::string filename = (tokens.size() > 2) ? tokens[2] : ".kvdb_history";
        if (read_history(filename.c_str()) == 0) {
            if (syntax_highlighting_) {
                std::cout << GREEN << "History loaded from " << filename << RESET << std::endl;
            } else {
                std::cout << "History loaded from " << filename << std::endl;
            }
        } else {
            if (syntax_highlighting_) {
                std::cout << RED << "Failed to load history from " << filename << RESET << std::endl;
            } else {
                std::cout << "Failed to load history from " << filename << std::endl;
            }
        }
    } else {
        std::cout << "Invalid action: " << action << std::endl;
        std::cout << "Valid actions: SHOW, CLEAR, SAVE, LOAD" << std::endl;
    }
#else
    std::cout << "History management requires readline support." << std::endl;
#endif
}

void REPL::cmd_clear() {
    // 清屏
    std::cout << "\033[2J\033[H";
    
    if (syntax_highlighting_) {
        std::cout << BOLD << CYAN << "KVDB v1.0.0" << RESET << " - Enhanced Command Line Interface\n";
        std::cout << GREEN << "Screen cleared." << RESET << std::endl;
    } else {
        std::cout << "KVDB v1.0.0 - Enhanced Command Line Interface\n";
        std::cout << "Screen cleared." << std::endl;
    }
}

void REPL::cmd_echo(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: ECHO <message>\n";
        std::cout << "Display a message\n";
        return;
    }
    
    // 连接所有参数作为消息
    std::string message;
    for (size_t i = 1; i < tokens.size(); i++) {
        if (i > 1) message += " ";
        message += tokens[i];
    }
    
    if (syntax_highlighting_) {
        std::cout << GREEN << message << RESET << std::endl;
    } else {
        std::cout << message << std::endl;
    }
}

// 高级查询命令实现
void REPL::cmd_batch_put(const std::vector<std::string>& tokens) {
    if (tokens.size() < 4 || (tokens.size() - 2) % 2 != 0) {
        std::cout << "Usage: BATCH PUT <key1> <value1> [key2] [value2] ...\n";
        std::cout << "Example: BATCH PUT user:1 John user:2 Jane user:3 Bob\n";
        return;
    }
    
    std::vector<std::pair<std::string, std::string>> pairs;
    for (size_t i = 2; i < tokens.size(); i += 2) {
        pairs.emplace_back(tokens[i], tokens[i + 1]);
    }
    
    if (query_engine_->batch_put(pairs)) {
        std::cout << "Batch PUT completed: " << pairs.size() << " key-value pairs inserted\n";
    } else {
        std::cout << "Batch PUT failed\n";
    }
}

void REPL::cmd_batch_get(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        std::cout << "Usage: BATCH GET <key1> [key2] [key3] ...\n";
        std::cout << "Example: BATCH GET user:1 user:2 user:3\n";
        return;
    }
    
    std::vector<std::string> keys;
    for (size_t i = 2; i < tokens.size(); i++) {
        keys.push_back(tokens[i]);
    }
    
    QueryResult result = query_engine_->batch_get(keys);
    
    if (result.success) {
        std::cout << "=== Batch GET Results ===\n";
        for (const auto& pair : result.results) {
            std::cout << pair.first << " = " << pair.second << "\n";
        }
        std::cout << "Found " << result.total_count << " out of " << keys.size() << " keys\n";
    } else {
        std::cout << "Batch GET failed: " << result.error_message << "\n";
    }
}

void REPL::cmd_batch_del(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        std::cout << "Usage: BATCH DEL <key1> [key2] [key3] ...\n";
        std::cout << "Example: BATCH DEL user:1 user:2 user:3\n";
        return;
    }
    
    std::vector<std::string> keys;
    for (size_t i = 2; i < tokens.size(); i++) {
        keys.push_back(tokens[i]);
    }
    
    if (query_engine_->batch_delete(keys)) {
        std::cout << "Batch DELETE completed: " << keys.size() << " keys deleted\n";
    } else {
        std::cout << "Batch DELETE failed\n";
    }
}

void REPL::cmd_get_where(const std::vector<std::string>& tokens) {
    if (tokens.size() < 5) {
        std::cout << "Usage: GET_WHERE <field> <operator> <value> [LIMIT <n>]\n";
        std::cout << "Fields: key, value\n";
        std::cout << "Operators: =, !=, LIKE, NOT_LIKE, >, <, >=, <=\n";
        std::cout << "Examples:\n";
        std::cout << "  GET_WHERE key LIKE 'user:*'\n";
        std::cout << "  GET_WHERE value > '100'\n";
        std::cout << "  GET_WHERE key = 'config:timeout' LIMIT 10\n";
        return;
    }
    
    std::string field = tokens[1];
    std::string op_str = tokens[2];
    std::string value = tokens[3];
    
    // 解析操作符
    ConditionOperator op;
    if (op_str == "=" || op_str == "==") {
        op = ConditionOperator::EQUALS;
    } else if (op_str == "!=" || op_str == "<>") {
        op = ConditionOperator::NOT_EQUALS;
    } else if (op_str == "LIKE") {
        op = ConditionOperator::LIKE;
    } else if (op_str == "NOT_LIKE") {
        op = ConditionOperator::NOT_LIKE;
    } else if (op_str == ">") {
        op = ConditionOperator::GREATER_THAN;
    } else if (op_str == "<") {
        op = ConditionOperator::LESS_THAN;
    } else if (op_str == ">=") {
        op = ConditionOperator::GREATER_EQUAL;
    } else if (op_str == "<=") {
        op = ConditionOperator::LESS_EQUAL;
    } else {
        std::cout << "Invalid operator: " << op_str << "\n";
        return;
    }
    
    // 解析限制
    size_t limit = 0;
    if (tokens.size() >= 6 && tokens[4] == "LIMIT") {
        try {
            limit = std::stoull(tokens[5]);
        } catch (const std::exception&) {
            std::cout << "Invalid limit value: " << tokens[5] << "\n";
            return;
        }
    }
    
    QueryCondition condition(field, op, value);
    QueryResult result = query_engine_->query_where(condition, limit);
    
    if (result.success) {
        std::cout << "=== Query Results ===\n";
        for (const auto& pair : result.results) {
            std::cout << pair.first << " = " << pair.second << "\n";
        }
        std::cout << "Found " << result.total_count << " matching records\n";
    } else {
        std::cout << "Query failed: " << result.error_message << "\n";
    }
}

void REPL::cmd_count(const std::vector<std::string>& tokens) {
    if (tokens.size() == 1) {
        // COUNT - 计算所有记录数
        AggregateResult result = query_engine_->count_all();
        if (result.success) {
            std::cout << "Total records: " << result.count << "\n";
        } else {
            std::cout << "Count failed: " << result.error_message << "\n";
        }
    } else if (tokens.size() >= 4) {
        // COUNT WHERE field operator value
        std::string field = tokens[2];
        std::string op_str = tokens[3];
        std::string value = tokens[4];
        
        // 解析操作符
        ConditionOperator op;
        if (op_str == "=" || op_str == "==") {
            op = ConditionOperator::EQUALS;
        } else if (op_str == "LIKE") {
            op = ConditionOperator::LIKE;
        } else {
            std::cout << "Unsupported operator for COUNT: " << op_str << "\n";
            return;
        }
        
        QueryCondition condition(field, op, value);
        AggregateResult result = query_engine_->count_where(condition);
        
        if (result.success) {
            std::cout << "Matching records: " << result.count << "\n";
        } else {
            std::cout << "Count failed: " << result.error_message << "\n";
        }
    } else {
        std::cout << "Usage:\n";
        std::cout << "  COUNT                           - Count all records\n";
        std::cout << "  COUNT WHERE <field> <op> <val>  - Count matching records\n";
        std::cout << "Examples:\n";
        std::cout << "  COUNT\n";
        std::cout << "  COUNT WHERE key LIKE 'user:*'\n";
    }
}

void REPL::cmd_sum(const std::vector<std::string>& tokens) {
    std::string pattern = "";
    if (tokens.size() >= 2) {
        pattern = tokens[1];
    }
    
    AggregateResult result = query_engine_->sum_values(pattern);
    
    if (result.success) {
        std::cout << "=== Sum Results ===\n";
        std::cout << "Count: " << result.count << "\n";
        std::cout << "Sum: " << result.sum << "\n";
        std::cout << "Average: " << result.avg << "\n";
    } else {
        std::cout << "Sum failed: " << result.error_message << "\n";
    }
    
    if (tokens.size() < 2) {
        std::cout << "\nUsage: SUM [key_pattern]\n";
        std::cout << "Example: SUM 'score:*' - Sum all values for keys matching 'score:*'\n";
    }
}

void REPL::cmd_avg(const std::vector<std::string>& tokens) {
    std::string pattern = "";
    if (tokens.size() >= 2) {
        pattern = tokens[1];
    }
    
    AggregateResult result = query_engine_->avg_values(pattern);
    
    if (result.success) {
        std::cout << "=== Average Results ===\n";
        std::cout << "Count: " << result.count << "\n";
        std::cout << "Sum: " << result.sum << "\n";
        std::cout << "Average: " << result.avg << "\n";
    } else {
        std::cout << "Average failed: " << result.error_message << "\n";
    }
    
    if (tokens.size() < 2) {
        std::cout << "\nUsage: AVG [key_pattern]\n";
        std::cout << "Example: AVG 'price:*' - Average all values for keys matching 'price:*'\n";
    }
}

void REPL::cmd_min_max(const std::vector<std::string>& tokens) {
    std::string pattern = "";
    if (tokens.size() >= 2) {
        pattern = tokens[1];
    }
    
    AggregateResult result = query_engine_->min_max_values(pattern);
    
    if (result.success) {
        std::cout << "=== Min/Max Results ===\n";
        std::cout << "Count: " << result.count << "\n";
        std::cout << "Sum: " << result.sum << "\n";
        std::cout << "Average: " << result.avg << "\n";
        std::cout << "Minimum: " << result.min << "\n";
        std::cout << "Maximum: " << result.max << "\n";
    } else {
        std::cout << "Min/Max failed: " << result.error_message << "\n";
    }
    
    if (tokens.size() < 2) {
        std::cout << "\nUsage: MIN_MAX [key_pattern]\n";
        std::cout << "Example: MIN_MAX 'temperature:*' - Min/Max for keys matching 'temperature:*'\n";
    }
}

void REPL::cmd_scan_ordered(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "Usage: SCAN_ORDER <ASC|DESC> [start_key] [end_key] [LIMIT <n>]\n";
        std::cout << "Examples:\n";
        std::cout << "  SCAN_ORDER ASC                    - Scan all in ascending order\n";
        std::cout << "  SCAN_ORDER DESC user:1 user:9     - Scan range in descending order\n";
        std::cout << "  SCAN_ORDER ASC '' '' LIMIT 10     - First 10 records in ascending order\n";
        return;
    }
    
    std::string order_str = tokens[1];
    SortOrder order;
    if (order_str == "ASC") {
        order = SortOrder::ASC;
    } else if (order_str == "DESC") {
        order = SortOrder::DESC;
    } else {
        std::cout << "Invalid order: " << order_str << ". Use ASC or DESC\n";
        return;
    }
    
    std::string start_key = "";
    std::string end_key = "";
    size_t limit = 0;
    
    if (tokens.size() >= 3) {
        start_key = tokens[2];
    }
    if (tokens.size() >= 4) {
        end_key = tokens[3];
    }
    
    // 解析LIMIT
    for (size_t i = 4; i < tokens.size(); i++) {
        if (tokens[i] == "LIMIT" && i + 1 < tokens.size()) {
            try {
                limit = std::stoull(tokens[i + 1]);
            } catch (const std::exception&) {
                std::cout << "Invalid limit value: " << tokens[i + 1] << "\n";
                return;
            }
            break;
        }
    }
    
    QueryResult result = query_engine_->scan_ordered(start_key, end_key, order, limit);
    
    if (result.success) {
        std::cout << "=== Ordered Scan Results ===\n";
        for (const auto& pair : result.results) {
            std::cout << pair.first << " = " << pair.second << "\n";
        }
        std::cout << "Found " << result.total_count << " records (order: " 
                  << (order == SortOrder::ASC ? "ASC" : "DESC") << ")\n";
    } else {
        std::cout << "Ordered scan failed: " << result.error_message << "\n";
    }
}