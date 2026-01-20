#include "kv_db.h"
#include <iostream>

KVDB::KVDB(const std::string& wal_file)
    : wal_(wal_file) {

        std::cout << "📂 正在从 WAL 文件重放数据: " << wal_file << std::endl;
        
        int put_count = 0, del_count = 0;
        wal_.replay(
            [this, &put_count](const std::string& key, const std::string& value) {
                memtable_.put(key, value);
                put_count++;
            },
            [this, &del_count](const std::string& key) {
                memtable_.del(key);
                del_count++;
            }
        );
        
        std::cout << "✅ WAL 重放完成: PUT " << put_count << " 次, DEL " << del_count << " 次" << std::endl;
    }

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
