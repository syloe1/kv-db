#pragma once
#include <optional>
#include <string>
#include <cstdint>
#include "cache/block_cache.h"
#include "sstable/block_index.h"

class SSTableReader {
public:
    // 带 snapshot_seq 的 get：查找 key 在 snapshot_seq 时刻的可见版本
    static std::optional<std::string>
    get(const std::string& filename, const std::string& key, uint64_t snapshot_seq, BlockCache& cache);
    
    // 兼容旧接口（使用最大序列号）
    static std::optional<std::string>
    get(const std::string& filename, const std::string& key, BlockCache& cache);
    
    // Enhanced get with block index optimization
    static std::optional<std::string>
    get_with_block_index(const std::string& filename, const std::string& key, 
                        uint64_t snapshot_seq, BlockCache& cache);
    
private:
    // Check if file uses enhanced format
    static bool is_enhanced_format(const std::string& filename);
    
    // Read data from specific block
    static std::optional<std::string>
    read_from_block(std::ifstream& in, const BlockIndexEntry& block_entry,
                   const std::string& key, uint64_t snapshot_seq);
    
    // Parse delta-encoded sequence numbers
    static std::vector<std::pair<uint64_t, std::string>>
    parse_delta_encoded_versions(const std::string& line);
};
