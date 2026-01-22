#pragma once
#include "iterator/iterator.h"
#include "sstable/sstable_meta.h"
#include <fstream>
#include <vector>
#include <cstdint>

struct SSTableFooter {
    uint64_t index_offset;
    uint64_t bloom_offset;
};

class SSTableIterator : public Iterator {
public:
    SSTableIterator(const SSTableMeta& meta, uint64_t snapshot_seq);
    ~SSTableIterator();

    void seek(const std::string& target) override;
    void seek_to_first() override;
    void seek_with_prefix(const std::string& prefix) override;
    void next() override;
    bool valid() const override;

    std::string key() const override;
    std::string value() const override;

private:
    void load_index();
    void load_data_block();
    void find_next_valid_version();
    bool key_matches_prefix() const;
    
    SSTableMeta meta_;
    uint64_t snapshot_seq_;
    std::ifstream file_;
    
    // Index: key -> offset
    std::vector<std::pair<std::string, uint64_t>> index_;
    int current_index_pos_;
    
    // Current data block for current key
    std::vector<std::pair<uint64_t, std::string>> current_versions_; // (seq, value)
    int current_version_pos_;
    
    bool is_valid_;
    std::string current_key_;
    std::string current_value_;
    
    // Prefix 优化
    std::string prefix_filter_;
    bool use_prefix_filter_;
};
