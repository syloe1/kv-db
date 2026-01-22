#pragma once
#include "version.h"
#include "sstable/sstable_meta.h"
#include <fstream>
#include <string>
#include <algorithm>

class VersionSet {
public:
    VersionSet(int max_level) 
        : max_level_(max_level), current_(max_level) {}

    const Version& current() const {
        return current_;
    }

    void add_file(int level, const SSTableMeta& meta) {
        if (level >= 0 && level < max_level_) {
            current_.levels[level].push_back(meta);
        }
    }

    void delete_file(int level, const std::string& filename) {
        if (level >= 0 && level < max_level_) {
            auto& vec = current_.levels[level];
            vec.erase(
                std::remove_if(vec.begin(), vec.end(),
                    [&](const auto& m) { return m.filename == filename; }),
                vec.end()
            );
        }
    }

    void recover();
    void persist_add(const SSTableMeta& meta, int level);
    void persist_del(const std::string& filename, int level);

private:
    int max_level_;
    Version current_;
};