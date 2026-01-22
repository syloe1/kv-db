#include "sstable/sstable_meta_util.h"
#include "sstable/sstable_reader.h"
#include <fstream>
#include <sstream>
#include <filesystem>

std::pair<std::string, std::string> 
SSTableMetaUtil::get_key_range_from_file(const std::string& filename) {
    std::ifstream in(filename);
    if (!in.is_open()) {
        return {"", ""};
    }
    
    // 先读取footer获取index_offset
    in.seekg(0, std::ios::end);
    std::streampos file_size = in.tellg();
    
    std::string last_line;
    long pos = (long)file_size - 1;
    
    in.seekg(pos);
    char c;
    while (pos > 0 && in.get(c) && c == '\n') {
        pos--;
        in.seekg(pos);
    }
    
    while (pos > 0) {
        in.seekg(pos);
        in.get(c);
        if (c == '\n') {
            pos++;
            break;
        }
        pos--;
    }
    
    in.clear();
    in.seekg(pos);
    std::getline(in, last_line);
    
    uint64_t index_offset = 0;
    std::istringstream footer_iss(last_line);
    footer_iss >> index_offset; // 读取index_offset，忽略bloom_offset
    
    // 只读取数据部分（从文件开始到index_offset）
    in.clear();
    in.seekg(0);
    
    std::string first_key, last_key;
    std::string line;
    uint64_t current_pos = 0;
    
    // 读取第一个key（格式：key seq value）
    if (std::getline(in, line)) {
        current_pos = in.tellg();
        if (current_pos > index_offset) {
            return {"", ""};
        }
        std::istringstream iss(line);
        std::string key;
        uint64_t seq;
        std::string value;
        if (iss >> key >> seq >> value) {
            first_key = key;
            last_key = key;
        }
    }
    
    // 继续读取数据部分，直到index_offset
    while (std::getline(in, line)) {
        current_pos = in.tellg();
        if (current_pos >= index_offset) {
            break;
        }
        
        std::istringstream iss(line);
        std::string key;
        uint64_t seq;
        std::string value;
        if (iss >> key >> seq >> value) {
            last_key = key;
        }
    }
    
    return {first_key, last_key};
}

SSTableMeta SSTableMetaUtil::get_meta_from_file(const std::string& filename) {
    auto [min_key, max_key] = get_key_range_from_file(filename);
    size_t file_size = std::filesystem::file_size(filename);
    
    return SSTableMeta(filename, min_key, max_key, file_size);
}