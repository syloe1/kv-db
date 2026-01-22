#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include "storage/versioned_value.h"
#include "sstable/block_index.h"

class SSTableWriter {
public:
    // Configuration for block-based writing
    struct Config {
        uint32_t block_size;        // Target block size in bytes
        uint32_t sparse_index_interval; // Sparse index every N keys
        bool enable_prefix_compression;
        bool enable_delta_encoding;
        
        Config() : block_size(4096), sparse_index_interval(16), 
                  enable_prefix_compression(true), enable_delta_encoding(true) {}
    };
    
    // 写入多版本数据：map<key, vector<VersionedValue>>
    // 数据按 key 排序，key 相同按 seq DESC 排序
    static void write(
        const std::string& filename,
        const std::map<std::string, std::vector<VersionedValue>>& data
    );
    
    // Enhanced write with block index optimization
    static void write_with_block_index(
        const std::string& filename,
        const std::map<std::string, std::vector<VersionedValue>>& data,
        const Config& config = Config()
    );
    
private:
    // Write a single data block
    static uint32_t write_data_block(
        std::ostream& out,
        const std::vector<std::pair<std::string, std::vector<VersionedValue>>>& block_data,
        const Config& config
    );
    
    // Calculate optimal block boundaries
    static std::vector<std::vector<std::pair<std::string, std::vector<VersionedValue>>>>
    partition_into_blocks(
        const std::map<std::string, std::vector<VersionedValue>>& data,
        const Config& config
    );
};