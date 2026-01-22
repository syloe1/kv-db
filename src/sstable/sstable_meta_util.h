#pragma once
#include "sstable/sstable_meta.h"
#include <string>

class SSTableMetaUtil {
public:
    static SSTableMeta get_meta_from_file(const std::string& filename);
    
private:
    static std::pair<std::string, std::string> 
    get_key_range_from_file(const std::string& filename);
};