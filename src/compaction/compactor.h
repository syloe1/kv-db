#pragma once 
#include <vector>
#include <string>
#include <cstdint>

class Compactor {
public:
    static std::string compact (
        const std::vector<std::string>& sstables,
        const std::string& output_dir,
        const std::string& output_filename = "",
        uint64_t min_snapshot_seq = 0  // 最小活跃 snapshot seq，用于保留版本
    );
};