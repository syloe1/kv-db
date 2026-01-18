#include "kv_db.h"

KVDB::KVDB(const std::string& wal_file)
    : wal_(wal_file) {}

bool KVDB::put(const std::string& key, const std::string& value) {
    wal_.log_put(key, value);
    memtable_.put(key, value);
    return true;
}

bool KVDB::get(const std::string& key, std::string& value) {
    return memtable_.get(key, value);
}

bool KVDB::del(const std::string& key) {
    wal_.log_del(key);
    memtable_.del(key);
    return true;
}