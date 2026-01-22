#include "version/version_set.h"
#include <fstream>
#include <iostream>

void VersionSet::persist_add(const SSTableMeta& meta, int level) {
    std::ofstream ofs("MANIFEST", std::ios::app);
    ofs << "ADD " << level << " "
        << meta.filename << " "
        << meta.min_key << " "
        << meta.max_key << "\n";
}

void VersionSet::persist_del(const std::string& filename, int level) {
    std::ofstream ofs("MANIFEST", std::ios::app);
    ofs << "DEL " << level << " " << filename << "\n";
}

void VersionSet::recover() {
    current_.levels.clear();
    current_.levels.resize(max_level_);

    std::ifstream ifs("MANIFEST");
    if (!ifs.is_open()) {
        std::cout << "[VersionSet] MANIFEST 文件不存在，创建新数据库\n";
        return;
    }

    std::string op;
    while (ifs >> op) {
        if (op == "ADD") {
            int level;
            SSTableMeta meta("", "", "", 0);
            ifs >> level >> meta.filename >> meta.min_key >> meta.max_key;
            if (level >= 0 && level < max_level_) {
                current_.levels[level].push_back(meta);
            }
        } else if (op == "DEL") {
            int level;
            std::string filename;
            ifs >> level >> filename;
            if (level >= 0 && level < max_level_) {
                auto& vec = current_.levels[level];
                vec.erase(
                    std::remove_if(vec.begin(), vec.end(),
                        [&](const auto& m) { return m.filename == filename; }),
                    vec.end()
                );
            }
        }
    }
    
    std::cout << "[VersionSet] 从 MANIFEST 恢复完成\n";
}