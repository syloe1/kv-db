#pragma once
#include "iterator/iterator.h"
#include "storage/memtable.h"
#include <map>
#include <vector>
#include <cstdint>

class MemTableIterator : public Iterator {
public:
    MemTableIterator(const MemTable& mem, uint64_t snapshot_seq);

    void seek(const std::string& target) override;
    void seek_to_first() override;
    void seek_with_prefix(const std::string& prefix) override;
    void next() override;
    bool valid() const override;

    std::string key() const override;
    std::string value() const override;

private:
    bool key_matches_prefix() const;
    
    const MemTable& mem_;
    uint64_t snapshot_seq_;
    std::map<std::string, std::vector<VersionedValue>>::const_iterator it_;
    
    // Prefix 优化
    std::string prefix_filter_;
    bool use_prefix_filter_;
};
