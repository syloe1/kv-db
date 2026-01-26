// 增强功能实现
// 这个文件包含了一些额外的功能实现

#include "query/query_engine.h"
#include <iostream>
#include <fstream>
#include <sstream>

// 扩展QueryEngine类的功能
class EnhancedQueryEngine : public QueryEngine {
public:
    explicit EnhancedQueryEngine(KVDB& db) : QueryEngine(db) {}
    
    // 导出数据功能
    bool export_data(const std::string& filename, const std::string& format = "JSON") {
        try {
            std::ofstream file(filename);
            if (!file.is_open()) {
                return false;
            }
            
            auto iter = create_iterator();
            iter->seek_to_first();
            
            if (format == "JSON") {
                file << "{\n";
                bool first = true;
                while (iter->valid()) {
                    if (!first) file << ",\n";
                    file << "  \"" << iter->key() << "\": \"" << iter->value() << "\"";
                    first = false;
                    iter->next();
                }
                file << "\n}\n";
            } else if (format == "CSV") {
                file << "key,value\n";
                while (iter->valid()) {
                    file << "\"" << iter->key() << "\",\"" << iter->value() << "\"\n";
                    iter->next();
                }
            } else {
                // KVDB format
                while (iter->valid()) {
                    file << "PUT " << iter->key() << " \"" << iter->value() << "\"\n";
                    iter->next();
                }
            }
            
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    // 导入数据功能
    bool import_data(const std::string& filename, const std::string& format = "JSON") {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                return false;
            }
            
            if (format == "KVDB") {
                std::string line;
                while (std::getline(file, line)) {
                    if (line.empty() || line[0] == '#') continue;
                    
                    std::istringstream iss(line);
                    std::string cmd, key, value;
                    iss >> cmd >> key;
                    
                    if (cmd == "PUT") {
                        std::getline(iss, value);
                        // 移除前导空格和引号
                        value = value.substr(value.find_first_not_of(" \t"));
                        if (value.front() == '"' && value.back() == '"') {
                            value = value.substr(1, value.length() - 2);
                        }
                        
                        std::vector<std::pair<std::string, std::string>> pairs = {{key, value}};
                        batch_put(pairs);
                    }
                }
            }
            
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    // 键存在检查
    bool key_exists(const std::string& key) {
        std::string value;
        return db_.get(key, value);
    }
    
    // 模式匹配的键列表
    std::vector<std::string> get_keys_matching(const std::string& pattern) {
        std::vector<std::string> keys;
        
        auto iter = create_iterator();
        iter->seek_to_first();
        
        while (iter->valid()) {
            std::string key = iter->key();
            if (match_pattern(key, pattern)) {
                keys.push_back(key);
            }
            iter->next();
        }
        
        return keys;
    }
    
    // 获取数据库信息
    struct DatabaseInfo {
        size_t total_keys = 0;
        size_t total_size = 0;
        std::map<std::string, size_t> prefix_counts;
    };
    
    DatabaseInfo get_database_info() {
        DatabaseInfo info;
        
        auto iter = create_iterator();
        iter->seek_to_first();
        
        while (iter->valid()) {
            std::string key = iter->key();
            std::string value = iter->value();
            
            info.total_keys++;
            info.total_size += key.size() + value.size();
            
            // 统计前缀
            size_t colon_pos = key.find(':');
            if (colon_pos != std::string::npos) {
                std::string prefix = key.substr(0, colon_pos + 1);
                info.prefix_counts[prefix]++;
            }
            
            iter->next();
        }
        
        return info;
    }

private:
    KVDB& db_;
    
    std::unique_ptr<Iterator> create_iterator() {
        Snapshot snap = db_.get_snapshot();
        return db_.new_iterator(snap);
    }
    
    bool match_pattern(const std::string& text, const std::string& pattern) {
        // 简单的通配符匹配
        if (pattern.find('*') != std::string::npos) {
            std::string prefix = pattern.substr(0, pattern.find('*'));
            return text.find(prefix) == 0;
        }
        return text == pattern;
    }
};