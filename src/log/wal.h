#pragma once
#include <fstream>
#include <string>
#include <functional>

class WAL {
public:
    explicit WAL(const std::string& filename);
    void log_put(const std::string& key, const std::string& value);
    void log_del(const std::string& key);

    void replay(
        const std::function<void(const std::string& ,const std::string&)>& on_put,
        const std::function<void(const std::string&)>& on_del
    );
private:
    std::ofstream file_;
    std::string filename_;
};
