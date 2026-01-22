<<<<<<< HEAD
#include "storage/memtable.h"

static const std::string TOMBSTONE = "__TOMBSTONE__";

void MemTable::put(const std::string& key, const std::string& value, uint64_t seq) {
    // 新版本永远插在最前（push_back，然后按 seq DESC 排序）
    // 为了简化，我们直接 push_back，读取时从后往前找
    table_[key].push_back({seq, value});
    size_bytes_ += key.size() + value.size() + sizeof(uint64_t);
}

bool MemTable::get(const std::string& key, uint64_t snapshot_seq, std::string& value) const {
    auto it = table_.find(key);
    if (it == table_.end()) return false;

    const auto& versions = it->second;
    // 从后往前遍历（最新版本在前）
    for (auto rit = versions.rbegin(); rit != versions.rend(); ++rit) {
        if (rit->seq <= snapshot_seq) {
            if (rit->value == TOMBSTONE) return false;
            value = rit->value;
            return true;
        }
    }
    return false;
}

void MemTable::del(const std::string& key, uint64_t seq) {
    // Tombstone is also a value
    put(key, TOMBSTONE, seq);
}

size_t MemTable::size() const {
    return size_bytes_;
}

std::map<std::string, std::vector<VersionedValue>> MemTable::get_all_versions() const {
    return table_;
}

void MemTable::clear() {
    table_.clear();
    size_bytes_ = 0;
}
=======
#include "memtable.h"

void MemTable::put(const std::string& key, const std::string& value) {
    table_[key] = value;
}

bool MemTable::get(const std::string& key, std::string& value) {
    auto it = table_.find(key);
    if (it == table_.end()) return false;
    value = it->second;
    return true;
}

void MemTable::del(const std::string& key) {
    table_.erase(key);
}
>>>>>>> cc24aa4eae4edea13c40a5b76ae3281181c6a76a
