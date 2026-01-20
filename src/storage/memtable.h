#pragma once
#include <map>
#include <string>

class MemTable {
public:
    void put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value); 
    void del(const std::string& key);
private:
    std::map<std::string, std::string> table_;
};