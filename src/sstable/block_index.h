#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

// Block-level index entry
struct BlockIndexEntry {
    std::string first_key;      // First key in the block
    std::string last_key;       // Last key in the block
    uint64_t offset;            // Block offset in file
    uint32_t size;              // Block size in bytes
    uint32_t num_entries;       // Number of entries in block
    
    BlockIndexEntry(const std::string& first, const std::string& last, 
                   uint64_t off, uint32_t sz, uint32_t entries)
        : first_key(first), last_key(last), offset(off), size(sz), num_entries(entries) {}
};

// Sparse index entry with prefix compression
struct SparseIndexEntry {
    std::string key_suffix;     // Key suffix after prefix compression
    uint32_t block_id;          // Block ID in block index
    uint16_t prefix_len;        // Length of common prefix
    
    SparseIndexEntry(const std::string& suffix, uint32_t block, uint16_t prefix)
        : key_suffix(suffix), block_id(block), prefix_len(prefix) {}
};

// Delta-encoded sequence numbers
struct DeltaEncodedSeq {
    uint64_t base_seq;          // Base sequence number
    std::vector<uint32_t> deltas; // Delta values from base
    
    DeltaEncodedSeq(uint64_t base) : base_seq(base) {}
    
    void add_seq(uint64_t seq) {
        if (seq >= base_seq) {
            deltas.push_back(static_cast<uint32_t>(seq - base_seq));
        }
    }
    
    uint64_t get_seq(size_t index) const {
        if (index < deltas.size()) {
            return base_seq + deltas[index];
        }
        return 0;
    }
};

// Enhanced block index with optimizations
class BlockIndex {
private:
    std::vector<BlockIndexEntry> block_entries_;
    std::vector<SparseIndexEntry> sparse_entries_;
    std::string common_prefix_;
    
public:
    // Add a block to the index
    void add_block(const std::string& first_key, const std::string& last_key,
                   uint64_t offset, uint32_t size, uint32_t num_entries);
    
    // Add sparse index entry with prefix compression
    void add_sparse_entry(const std::string& key, uint32_t block_id);
    
    // Find block containing the key
    int find_block(const std::string& key) const;
    
    // Get block entry by ID
    const BlockIndexEntry* get_block(uint32_t block_id) const;
    
    // Serialize index to stream
    void serialize(std::ostream& out) const;
    
    // Deserialize index from stream
    void deserialize(std::istream& in);
    
    // Get index size in bytes
    size_t get_size() const;
    
    // Get number of blocks
    size_t get_block_count() const { return block_entries_.size(); }
    
    // Get common prefix
    const std::string& get_common_prefix() const { return common_prefix_; }
    
private:
    // Calculate common prefix for all keys
    void calculate_common_prefix(const std::vector<std::string>& keys);
    
    // Compress key using common prefix
    std::string compress_key(const std::string& key) const;
    
    // Decompress key using common prefix
    std::string decompress_key(const std::string& compressed_key, uint16_t prefix_len) const;
};