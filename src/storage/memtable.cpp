#include "memtable.h"

void MemTable::put(const std::string& key, const std::string& value) {
    table_[key] = value;
}

bool MemTable::get(const std::string& key, std::string& value) {
    auto it = table_.find(key);
    if (it == table_.end()) return false;
    value = it->second;
    return true;
}

void MemTable::del(const std::string& key) {
    table_.erase(key);
}