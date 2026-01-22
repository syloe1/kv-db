#pragma once
#include "sstable/sstable_meta.h"
#include <vector>

struct Version {
    std::vector<std::vector<SSTableMeta>> levels;
    
    Version(int max_level) {
        levels.resize(max_level);
    }
};