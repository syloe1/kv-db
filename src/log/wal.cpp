#include "wal.h"

WAL::WAL(const std::string& filename) {
    file_.open(filename, std::ios::app);
}

void WAL::log_put(const std::string& key, const std::string& value) {
    file_ << "PUT " << key << " " << value << "\n";
    file_.flush();
}

void WAL::log_del(const std::string& key) {
    file_ << "DEL " << key << "\n";
    file_.flush();
}