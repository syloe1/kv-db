#include "wal.h"
#include <fstream>
#include <sstream>

WAL::WAL(const std::string& filename)
    : filename_(filename) {
    file_.open(filename_, std::ios::app);
}

void WAL::log_put(const std::string& key, const std::string& value) {
    file_<< "PUT "<< key << " " << value << "\n";
    file_.flush();
}

void WAL::log_del(const std::string& key) {
    file_ << "DEL " << key << '\n';
    file_.flush();
}

void WAL::replay(
    const std::function<void(const std::string&, const std::string&)>& on_put,
    const std::function<void(const std::string&)>& on_del
) {
    std::ifstream in(filename_);
    std::string line;
    while(std::getline(in, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if(cmd == "PUT") {
            std::string key, value;
            iss >> key >> value;
            on_put(key, value);
        } else if(cmd == "DEL") {
            std::string key;
            iss >> key;
            on_del(key);
        }
    }
}