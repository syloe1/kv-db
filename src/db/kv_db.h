#pragma once
#include "storage/memtable.h"
#include "log/wal.h"

class KVDB {
public:
    explicit KVDB(const std::string& wal_file);

    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    bool del(const std::string& key);

private:
    MemTable memtable_;
    WAL wal_;
};