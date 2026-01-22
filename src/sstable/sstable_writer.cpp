#include "sstable/sstable_writer.h"
#include "sstable/block_index.h"
#include "bloom/bloom_filter.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <sstream>

void SSTableWriter::write(
    const std::string& filename,
    const std::map<std::string, std::vector<VersionedValue>>& data
) {
    std::ofstream out(filename, std::ios::binary);
    std::vector<std::pair<std::string, uint64_t>> index;
    BloomFilter bloom(8192, 3);

    // 写入数据：key | seq | value
    // 按 key 排序，key 相同按 seq DESC 排序
    for (const auto& [key, versions] : data) {
        uint64_t offset = out.tellp();
        
        // 对每个 key 的所有版本，按 seq DESC 排序
        std::vector<VersionedValue> sorted_versions = versions;
        std::sort(sorted_versions.begin(), sorted_versions.end(),
                  [](const VersionedValue& a, const VersionedValue& b) {
                      return a.seq > b.seq; // DESC
                  });
        
        // 写入所有版本
        for (const auto& v : sorted_versions) {
            out << key << " " << v.seq << " " << v.value << '\n';
        }
        
        index.push_back({key, offset});
        bloom.add(key);
    }

    uint64_t index_offset = out.tellp();

    for(const auto& [key, offset] : index) {
        out<<key<<" "<<offset<<'\n';
    }

    uint64_t bloom_offset = out.tellp();
    bloom.serialize(out);
    out<<index_offset<<" "<<bloom_offset<<'\n';
    out.flush();
}

void SSTableWriter::write_with_block_index(
    const std::string& filename,
    const std::map<std::string, std::vector<VersionedValue>>& data,
    const Config& config
) {
    std::ofstream out(filename, std::ios::binary);
    BlockIndex block_index;
    BloomFilter bloom(8192, 3);
    
    // Partition data into blocks
    auto blocks = partition_into_blocks(data, config);
    
    uint64_t data_start_offset = out.tellp();
    std::vector<std::string> all_keys;
    
    // Write data blocks
    for (size_t block_id = 0; block_id < blocks.size(); ++block_id) {
        const auto& block_data = blocks[block_id];
        if (block_data.empty()) continue;
        
        uint64_t block_offset = out.tellp();
        std::string first_key = block_data.front().first;
        std::string last_key = block_data.back().first;
        
        // Collect keys for prefix compression
        for (const auto& [key, versions] : block_data) {
            all_keys.push_back(key);
            bloom.add(key);
        }
        
        // Write block data with delta encoding
        uint32_t block_size = write_data_block(out, block_data, config);
        
        // Add block to index
        block_index.add_block(first_key, last_key, block_offset, 
                             block_size, static_cast<uint32_t>(block_data.size()));
        
        // Add sparse index entries
        for (size_t i = 0; i < block_data.size(); i += config.sparse_index_interval) {
            block_index.add_sparse_entry(block_data[i].first, static_cast<uint32_t>(block_id));
        }
    }
    
    // Write block index
    uint64_t block_index_offset = out.tellp();
    block_index.serialize(out);
    
    // Write bloom filter
    uint64_t bloom_offset = out.tellp();
    bloom.serialize(out);
    
    // Write footer with enhanced format
    out << "ENHANCED_SSTABLE_V1\n";
    out << data_start_offset << " " << block_index_offset << " " << bloom_offset << "\n";
    out.flush();
}

uint32_t SSTableWriter::write_data_block(
    std::ostream& out,
    const std::vector<std::pair<std::string, std::vector<VersionedValue>>>& block_data,
    const Config& config
) {
    uint64_t start_pos = out.tellp();
    
    for (const auto& [key, versions] : block_data) {
        // Sort versions by sequence number DESC
        std::vector<VersionedValue> sorted_versions = versions;
        std::sort(sorted_versions.begin(), sorted_versions.end(),
                  [](const VersionedValue& a, const VersionedValue& b) {
                      return a.seq > b.seq; // DESC
                  });
        
        if (config.enable_delta_encoding && sorted_versions.size() > 1) {
            // Use delta encoding for sequence numbers
            DeltaEncodedSeq delta_seq(sorted_versions[0].seq);
            for (size_t i = 1; i < sorted_versions.size(); ++i) {
                delta_seq.add_seq(sorted_versions[i].seq);
            }
            
            // Write key and base sequence
            out << key << " " << delta_seq.base_seq << " " << sorted_versions[0].value;
            
            // Write delta-encoded versions
            for (size_t i = 1; i < sorted_versions.size(); ++i) {
                out << " " << (sorted_versions[i].seq - delta_seq.base_seq) 
                    << " " << sorted_versions[i].value;
            }
            out << '\n';
        } else {
            // Write all versions normally
            for (const auto& v : sorted_versions) {
                out << key << " " << v.seq << " " << v.value << '\n';
            }
        }
    }
    
    uint64_t end_pos = out.tellp();
    return static_cast<uint32_t>(end_pos - start_pos);
}

std::vector<std::vector<std::pair<std::string, std::vector<VersionedValue>>>>
SSTableWriter::partition_into_blocks(
    const std::map<std::string, std::vector<VersionedValue>>& data,
    const Config& config
) {
    std::vector<std::vector<std::pair<std::string, std::vector<VersionedValue>>>> blocks;
    std::vector<std::pair<std::string, std::vector<VersionedValue>>> current_block;
    uint32_t current_block_size = 0;
    
    for (const auto& [key, versions] : data) {
        // Estimate size of this entry
        uint32_t entry_size = key.length() + 20; // Approximate
        for (const auto& v : versions) {
            entry_size += v.value.length() + 12; // Approximate
        }
        
        // Check if we need to start a new block
        if (!current_block.empty() && 
            current_block_size + entry_size > config.block_size) {
            blocks.push_back(std::move(current_block));
            current_block.clear();
            current_block_size = 0;
        }
        
        current_block.emplace_back(key, versions);
        current_block_size += entry_size;
    }
    
    // Add the last block
    if (!current_block.empty()) {
        blocks.push_back(std::move(current_block));
    }
    
    return blocks;
}