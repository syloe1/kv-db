#pragma once
#include <fstream>
#include <string>

class WAL {
public:
    explicit WAL(const std::string& filename);
    void log_put(const std::string& key, const std::string& value);
    void log_del(const std::string& key);

private:
    std::ofstream file_;
};