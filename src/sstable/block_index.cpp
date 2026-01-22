#include "sstable/block_index.h"
#include <algorithm>
#include <sstream>
#include <iostream>

void BlockIndex::add_block(const std::string& first_key, const std::string& last_key,
                          uint64_t offset, uint32_t size, uint32_t num_entries) {
    block_entries_.emplace_back(first_key, last_key, offset, size, num_entries);
}

void BlockIndex::add_sparse_entry(const std::string& key, uint32_t block_id) {
    // Calculate prefix length with common prefix
    uint16_t prefix_len = 0;
    if (!common_prefix_.empty()) {
        prefix_len = std::min(key.length(), common_prefix_.length());
        while (prefix_len > 0 && key[prefix_len - 1] != common_prefix_[prefix_len - 1]) {
            prefix_len--;
        }
    }
    
    // Store suffix after prefix compression
    std::string suffix = (prefix_len < key.length()) ? key.substr(prefix_len) : "";
    sparse_entries_.emplace_back(suffix, block_id, prefix_len);
}

int BlockIndex::find_block(const std::string& key) const {
    // Binary search in block entries
    int left = 0, right = static_cast<int>(block_entries_.size()) - 1;
    int result = -1;
    
    while (left <= right) {
        int mid = left + (right - left) / 2;
        const auto& entry = block_entries_[mid];
        
        if (key >= entry.first_key && key <= entry.last_key) {
            return mid; // Found exact block
        }
        
        if (key < entry.first_key) {
            right = mid - 1;
        } else {
            result = mid; // This block might contain the key
            left = mid + 1;
        }
    }
    
    return result;
}

const BlockIndexEntry* BlockIndex::get_block(uint32_t block_id) const {
    if (block_id < block_entries_.size()) {
        return &block_entries_[block_id];
    }
    return nullptr;
}

void BlockIndex::serialize(std::ostream& out) const {
    // Write header
    out << "BLOCK_INDEX_V1\n";
    out << common_prefix_.length() << " " << common_prefix_ << "\n";
    out << block_entries_.size() << " " << sparse_entries_.size() << "\n";
    
    // Write block entries
    for (const auto& entry : block_entries_) {
        out << entry.first_key << "|" << entry.last_key << "|"
            << entry.offset << "|" << entry.size << "|" << entry.num_entries << "\n";
    }
    
    // Write sparse entries
    for (const auto& entry : sparse_entries_) {
        out << entry.key_suffix << "|" << entry.block_id << "|" << entry.prefix_len << "\n";
    }
}

void BlockIndex::deserialize(std::istream& in) {
    std::string line;
    
    // Read header
    std::getline(in, line);
    if (line != "BLOCK_INDEX_V1") {
        throw std::runtime_error("Invalid block index format");
    }
    
    // Read common prefix
    std::getline(in, line);
    std::istringstream prefix_stream(line);
    size_t prefix_len;
    prefix_stream >> prefix_len;
    if (prefix_len > 0) {
        prefix_stream.ignore(1); // Skip space
        std::getline(prefix_stream, common_prefix_);
    }
    
    // Read counts
    std::getline(in, line);
    std::istringstream count_stream(line);
    size_t block_count, sparse_count;
    count_stream >> block_count >> sparse_count;
    
    // Read block entries
    block_entries_.reserve(block_count);
    for (size_t i = 0; i < block_count; ++i) {
        std::getline(in, line);
        std::istringstream entry_stream(line);
        std::string first_key, last_key;
        uint64_t offset;
        uint32_t size, num_entries;
        
        std::getline(entry_stream, first_key, '|');
        std::getline(entry_stream, last_key, '|');
        entry_stream >> offset;
        entry_stream.ignore(1); // Skip '|'
        entry_stream >> size;
        entry_stream.ignore(1); // Skip '|'
        entry_stream >> num_entries;
        
        block_entries_.emplace_back(first_key, last_key, offset, size, num_entries);
    }
    
    // Read sparse entries
    sparse_entries_.reserve(sparse_count);
    for (size_t i = 0; i < sparse_count; ++i) {
        std::getline(in, line);
        std::istringstream entry_stream(line);
        std::string key_suffix;
        uint32_t block_id;
        uint16_t prefix_len;
        
        std::getline(entry_stream, key_suffix, '|');
        entry_stream >> block_id;
        entry_stream.ignore(1); // Skip '|'
        entry_stream >> prefix_len;
        
        sparse_entries_.emplace_back(key_suffix, block_id, prefix_len);
    }
}

size_t BlockIndex::get_size() const {
    size_t size = 0;
    
    // Header size
    size += 20; // Approximate header size
    size += common_prefix_.length();
    
    // Block entries size
    for (const auto& entry : block_entries_) {
        size += entry.first_key.length() + entry.last_key.length() + 20; // Approximate
    }
    
    // Sparse entries size
    for (const auto& entry : sparse_entries_) {
        size += entry.key_suffix.length() + 8; // Approximate
    }
    
    return size;
}

void BlockIndex::calculate_common_prefix(const std::vector<std::string>& keys) {
    if (keys.empty()) {
        return;
    }
    
    common_prefix_ = keys[0];
    
    for (size_t i = 1; i < keys.size(); ++i) {
        size_t j = 0;
        while (j < common_prefix_.length() && j < keys[i].length() &&
               common_prefix_[j] == keys[i][j]) {
            ++j;
        }
        common_prefix_ = common_prefix_.substr(0, j);
        
        if (common_prefix_.empty()) {
            break;
        }
    }
}

std::string BlockIndex::compress_key(const std::string& key) const {
    if (key.length() > common_prefix_.length() &&
        key.substr(0, common_prefix_.length()) == common_prefix_) {
        return key.substr(common_prefix_.length());
    }
    return key;
}

std::string BlockIndex::decompress_key(const std::string& compressed_key, uint16_t prefix_len) const {
    if (prefix_len > 0 && prefix_len <= common_prefix_.length()) {
        return common_prefix_.substr(0, prefix_len) + compressed_key;
    }
    return compressed_key;
}