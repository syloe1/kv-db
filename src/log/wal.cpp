#include "log/wal.h"
#include <fstream>
#include <sstream>
#include <functional>
#include <filesystem>
#include <sys/stat.h>
#include <iostream>

WAL::WAL(const std::string& filename)
    : filename_(filename) {
    // 确保目录存在（使用POSIX方法）
    size_t pos = filename.find_last_of('/');
    if (pos != std::string::npos) {
        std::string dir = filename.substr(0, pos);
        if (!dir.empty()) {
            // 创建目录（简化版，只创建一级目录）
            mkdir(dir.c_str(), 0755);
        }
    }
    
    // 以追加模式打开文件，如果文件不存在则创建
    file_.open(filename_, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "[WAL] 错误: 无法打开文件 " << filename_ << std::endl;
    } else {
        std::cout << "[WAL] 已打开文件: " << filename_ << std::endl;
    }
}

void WAL::log_put(const std::string& key, const std::string& value) {
    file_ << "PUT " << key << " " << value << "\n";
    file_.flush();
}

void WAL::log_del(const std::string& key) {
    file_ << "DEL " << key << "\n";
    file_.flush();
}

void WAL::replay(
    const std::function<void(const std::string&, const std::string&)>& on_put,
    const std::function<void(const std::string&)>& on_del
) {
    std::cout << "[WAL重放] 开始重放WAL文件: " << filename_ << std::endl;
    
    std::ifstream in(filename_);
    if (!in.is_open()) {
        std::cerr << "[WAL重放] 错误: 无法打开文件 " << filename_ << std::endl;
        return;
    }
    
    std::string line;
    int line_count = 0;
    
    while (std::getline(in, line)) {
        line_count++;
        std::cout << "[WAL重放] 第" << line_count << "行: " << line << std::endl;
        
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        
        if (cmd == "PUT") {
            std::string key, value;
            iss >> key >> value;
            std::cout << "[WAL重放] 执行PUT: key=" << key << ", value=" << value << std::endl;
            on_put(key, value);
        } else if (cmd == "DEL") {
            std::string key;
            iss >> key;
            std::cout << "[WAL重放] 执行DEL: key=" << key << std::endl;
            on_del(key);
        } else {
            std::cerr << "[WAL重放] 警告: 未知命令: " << cmd << std::endl;
        }
    }
    
    std::cout << "[WAL重放] 完成，共处理 " << line_count << " 行" << std::endl;
}
