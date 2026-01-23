#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>

// 模拟 REPL 的语法高亮功能
class SimpleCLIDemo {
private:
    std::unordered_map<std::string, std::string> color_map_;
    bool syntax_highlighting_;
    
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

public:
    SimpleCLIDemo() : syntax_highlighting_(true) {
        setup_colors();
    }
    
    void setup_colors() {
        color_map_["PUT"] = GREEN + BOLD;
        color_map_["GET"] = BLUE + BOLD;
        color_map_["DEL"] = RED + BOLD;
        color_map_["SCAN"] = CYAN + BOLD;
        color_map_["HELP"] = YELLOW + BOLD;
        color_map_["SOURCE"] = MAGENTA + BOLD;
        color_map_["MULTILINE"] = YELLOW;
        color_map_["HIGHLIGHT"] = YELLOW;
        color_map_["HISTORY"] = WHITE;
        color_map_["CLEAR"] = WHITE;
        color_map_["ECHO"] = GREEN;
        color_map_["EXIT"] = RED + BOLD;
    }
    
    std::string colorize_command(const std::string& command) {
        if (!syntax_highlighting_) return command;
        
        auto it = color_map_.find(command);
        if (it != color_map_.end()) {
            return it->second + command + RESET;
        }
        return RED + command + RESET;  // 未知命令用红色
    }
    
    std::string colorize_parameter(const std::string& param) {
        if (!syntax_highlighting_) return param;
        return CYAN + param + RESET;
    }
    
    void demonstrate_features() {
        std::cout << BOLD << CYAN << "KVDB v1.0.0" << RESET << " - Enhanced Command Line Interface Demo\n";
        std::cout << GREEN << "Features:" << RESET << " Syntax highlighting, multi-line input, script support\n\n";
        
        std::cout << BOLD << "1. 语法高亮演示:" << RESET << "\n";
        std::vector<std::string> commands = {
            "PUT user:1 Alice",
            "GET user:1", 
            "DEL user:1",
            "SCAN user:1 user:9",
            "SOURCE test.kvdb",
            "MULTILINE ON",
            "HIGHLIGHT OFF",
            "HISTORY SHOW",
            "HELP",
            "EXIT"
        };
        
        for (const auto& cmd_line : commands) {
            std::cout << "  kvdb> ";
            auto tokens = split(cmd_line);
            if (!tokens.empty()) {
                std::cout << colorize_command(tokens[0]);
                for (size_t i = 1; i < tokens.size(); i++) {
                    std::cout << " " << colorize_parameter(tokens[i]);
                }
            }
            std::cout << std::endl;
        }
        
        std::cout << "\n" << BOLD << "2. 多行输入演示:" << RESET << "\n";
        std::cout << "  kvdb> " << colorize_command("PUT") << " " << colorize_parameter("long_key") << " " << YELLOW << "\\" << RESET << "\n";
        std::cout << "  ...   " << colorize_parameter("\"This is a very long value") << " " << YELLOW << "\\" << RESET << "\n";
        std::cout << "  ...    " << colorize_parameter("that spans multiple lines\"") << "\n";
        
        std::cout << "\n" << BOLD << "3. 脚本支持演示:" << RESET << "\n";
        std::cout << "  " << DIM << "# test_script.kvdb" << RESET << "\n";
        std::cout << "  " << DIM << "# KVDB 测试脚本" << RESET << "\n";
        std::cout << "  " << colorize_command("ECHO") << " " << colorize_parameter("\"开始测试...\"") << "\n";
        std::cout << "  " << colorize_command("PUT") << " " << colorize_parameter("user:1") << " " << colorize_parameter("\"Alice\"") << "\n";
        std::cout << "  " << colorize_command("GET") << " " << colorize_parameter("user:1") << "\n";
        
        std::cout << "\n" << BOLD << "4. Tab 补全演示:" << RESET << "\n";
        std::cout << "  kvdb> SET_COMPACTION [TAB] → " << MAGENTA << "LEVELED TIERED SIZE_TIERED TIME_WINDOW" << RESET << "\n";
        std::cout << "  kvdb> BENCHMARK [TAB] → " << MAGENTA << "A B C D E F" << RESET << "\n";
        std::cout << "  kvdb> HISTORY [TAB] → " << MAGENTA << "SHOW CLEAR SAVE LOAD" << RESET << "\n";
        
        std::cout << "\n" << BOLD << "5. 新增命令演示:" << RESET << "\n";
        std::vector<std::pair<std::string, std::string>> new_commands = {
            {"SOURCE filename", "执行脚本文件"},
            {"MULTILINE ON/OFF", "控制多行输入模式"},
            {"HIGHLIGHT ON/OFF", "控制语法高亮"},
            {"HISTORY SHOW", "显示命令历史"},
            {"CLEAR", "清屏"},
            {"ECHO message", "显示消息"}
        };
        
        for (const auto& cmd : new_commands) {
            auto tokens = split(cmd.first);
            std::cout << "  " << colorize_command(tokens[0]);
            for (size_t i = 1; i < tokens.size(); i++) {
                std::cout << " " << colorize_parameter(tokens[i]);
            }
            std::cout << " - " << cmd.second << "\n";
        }
        
        std::cout << "\n" << GREEN << "✅ 命令行增强功能演示完成！" << RESET << "\n";
        std::cout << "所有功能都已实现并可以在实际的 KVDB CLI 中使用。\n";
    }
    
private:
    std::vector<std::string> split(const std::string& line) {
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }
    
    static const std::string DIM;
};

// 颜色常量定义
const std::string SimpleCLIDemo::RESET = "\033[0m";
const std::string SimpleCLIDemo::RED = "\033[31m";
const std::string SimpleCLIDemo::GREEN = "\033[32m";
const std::string SimpleCLIDemo::YELLOW = "\033[33m";
const std::string SimpleCLIDemo::BLUE = "\033[34m";
const std::string SimpleCLIDemo::MAGENTA = "\033[35m";
const std::string SimpleCLIDemo::CYAN = "\033[36m";
const std::string SimpleCLIDemo::WHITE = "\033[37m";
const std::string SimpleCLIDemo::BOLD = "\033[1m";
const std::string SimpleCLIDemo::DIM = "\033[2m";

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "    KVDB 命令行增强功能演示" << std::endl;
    std::cout << "========================================" << std::endl;
    
    SimpleCLIDemo demo;
    demo.demonstrate_features();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "    演示完成" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}