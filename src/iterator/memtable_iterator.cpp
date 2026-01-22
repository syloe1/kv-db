#include "iterator/memtable_iterator.h"
#include "storage/memtable.h"
#include <algorithm>

static const std::string TOMBSTONE = "__TOMBSTONE__";

MemTableIterator::MemTableIterator(const MemTable& mem, uint64_t snapshot_seq)
    : mem_(mem), snapshot_seq_(snapshot_seq), it_(mem.get_table().end()), use_prefix_filter_(false) {
}

void MemTableIterator::seek(const std::string& target) {
    use_prefix_filter_ = false;
    it_ = mem_.get_table().lower_bound(target);
}

void MemTableIterator::seek_to_first() {
    use_prefix_filter_ = false;
    it_ = mem_.get_table().begin();
}

void MemTableIterator::seek_with_prefix(const std::string& prefix) {
    use_prefix_filter_ = true;
    prefix_filter_ = prefix;
    it_ = mem_.get_table().lower_bound(prefix);
    
    // 检查当前 key 是否匹配前缀
    if (valid() && !key_matches_prefix()) {
        it_ = mem_.get_table().end(); // 没有匹配的 key
    }
}

bool MemTableIterator::key_matches_prefix() const {
    if (!use_prefix_filter_) return true;
    if (!valid()) return false;
    const std::string& key = it_->first;
    return key.size() >= prefix_filter_.size() && 
           key.substr(0, prefix_filter_.size()) == prefix_filter_;
}

void MemTableIterator::next() {
    if (valid()) {
        ++it_;
        
        // Prefix 过滤优化
        if (use_prefix_filter_ && valid() && !key_matches_prefix()) {
            it_ = mem_.get_table().end(); // 超出前缀范围
        }
    }
}

bool MemTableIterator::valid() const {
    return it_ != mem_.get_table().end();
}

std::string MemTableIterator::key() const {
    if (!valid()) return "";
    return it_->first;
}

std::string MemTableIterator::value() const {
    if (!valid()) return "";
    
    const auto& versions = it_->second;
    // 从后往前遍历，找到 <= snapshot_seq 的第一个版本
    for (auto rit = versions.rbegin(); rit != versions.rend(); ++rit) {
        if (rit->seq <= snapshot_seq_) {
            if (rit->value == TOMBSTONE) return ""; // Tombstone 会被 MergeIterator 过滤
            return rit->value;
        }
    }
    return "";
}
