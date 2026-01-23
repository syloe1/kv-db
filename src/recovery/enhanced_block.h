#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include "integrity_checker.h"

// Enhanced block header with CRC32 support
struct EnhancedBlockHeader {
    uint32_t magic_number;     // Magic number for format identification
    uint32_t version;          // Block format version
    uint32_t crc32;           // CRC32 checksum of block data
    uint32_t data_size;       // Size of actual data in bytes
    uint64_t timestamp;       // Block creation timestamp
    uint32_t block_type;      // Type of block (SSTable, WAL, MemTable)
    uint32_t reserved;        // Reserved for future use
    
    static constexpr uint32_t MAGIC_NUMBER = 0x4B564442; // "KVDB"
    static constexpr uint32_t CURRENT_VERSION = 1;
    
    enum BlockType {
        SSTABLE_BLOCK = 1,
        WAL_BLOCK = 2,
        MEMTABLE_BLOCK = 3,
        INDEX_BLOCK = 4
    };
    
    EnhancedBlockHeader() : magic_number(MAGIC_NUMBER), version(CURRENT_VERSION),
                           crc32(0), data_size(0), timestamp(0), 
                           block_type(SSTABLE_BLOCK), reserved(0) {}
    
    // Validate header integrity
    bool is_valid() const {
        return magic_number == MAGIC_NUMBER && version <= CURRENT_VERSION;
    }
    
    // Calculate header checksum (excluding crc32 field)
    uint32_t calculate_header_crc32() const;
    
    // Verify header checksum
    bool verify_header_crc32() const;
};

// Enhanced SSTable block with CRC32 protection
class EnhancedSSTableBlock {
private:
    EnhancedBlockHeader header_;
    std::vector<uint8_t> data_;
    
public:
    static constexpr size_t MAX_BLOCK_SIZE = 64 * 1024; // 64KB
    
    EnhancedSSTableBlock();
    explicit EnhancedSSTableBlock(const std::vector<uint8_t>& data);
    explicit EnhancedSSTableBlock(const std::string& text_data);
    
    // Data access methods
    void set_data(const std::vector<uint8_t>& data);
    void set_data(const std::string& text_data);
    const std::vector<uint8_t>& get_data() const { return data_; }
    std::string get_data_as_string() const;
    
    // Header access
    const EnhancedBlockHeader& get_header() const { return header_; }
    
    // Integrity operations
    void calculate_crc32();
    bool verify_integrity() const;
    IntegrityStatus validate() const;
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    static EnhancedSSTableBlock deserialize(const std::vector<uint8_t>& serialized_data);
    static EnhancedSSTableBlock deserialize(std::istream& in);
    
    // Size information
    size_t get_total_size() const;
    size_t get_data_size() const { return data_.size(); }
    
    // Backward compatibility
    bool is_legacy_format(const std::string& data) const;
    static EnhancedSSTableBlock from_legacy_data(const std::string& legacy_data);
};

// Enhanced WAL block with sequence number and CRC32
class EnhancedWALBlock {
private:
    EnhancedBlockHeader header_;
    uint64_t lsn_;            // Log sequence number
    std::vector<uint8_t> entry_data_;
    
public:
    static constexpr size_t MAX_BLOCK_SIZE = 32 * 1024; // 32KB
    
    EnhancedWALBlock();
    EnhancedWALBlock(uint64_t lsn, const std::vector<uint8_t>& entry_data);
    EnhancedWALBlock(uint64_t lsn, const std::string& entry_text);
    
    // LSN access
    uint64_t get_lsn() const { return lsn_; }
    void set_lsn(uint64_t lsn) { lsn_ = lsn; }
    
    // Entry data access
    void set_entry_data(const std::vector<uint8_t>& data);
    void set_entry_data(const std::string& text_data);
    const std::vector<uint8_t>& get_entry_data() const { return entry_data_; }
    std::string get_entry_data_as_string() const;
    
    // Header access
    const EnhancedBlockHeader& get_header() const { return header_; }
    
    // Integrity operations
    void calculate_crc32();
    bool verify_integrity() const;
    IntegrityStatus validate() const;
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    static EnhancedWALBlock deserialize(const std::vector<uint8_t>& serialized_data);
    static EnhancedWALBlock deserialize(std::istream& in);
    
    // Size information
    size_t get_total_size() const;
    size_t get_entry_size() const { return entry_data_.size(); }
    
    // Backward compatibility
    static EnhancedWALBlock from_legacy_entry(const std::string& legacy_entry, uint64_t lsn);
};

// Enhanced MemTable block for in-memory data with CRC32
class EnhancedMemTableBlock {
private:
    EnhancedBlockHeader header_;
    uint32_t entry_count_;
    std::vector<uint8_t> entries_data_;
    
public:
    static constexpr size_t MAX_BLOCK_SIZE = 16 * 1024; // 16KB
    
    EnhancedMemTableBlock();
    EnhancedMemTableBlock(const std::vector<uint8_t>& entries_data, uint32_t entry_count);
    
    // Entry management
    void set_entries_data(const std::vector<uint8_t>& data, uint32_t count);
    const std::vector<uint8_t>& get_entries_data() const { return entries_data_; }
    uint32_t get_entry_count() const { return entry_count_; }
    
    // Header access
    const EnhancedBlockHeader& get_header() const { return header_; }
    
    // Integrity operations
    void calculate_crc32();
    bool verify_integrity() const;
    IntegrityStatus validate() const;
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    static EnhancedMemTableBlock deserialize(const std::vector<uint8_t>& serialized_data);
    static EnhancedMemTableBlock deserialize(std::istream& in);
    
    // Size information
    size_t get_total_size() const;
    size_t get_entries_size() const { return entries_data_.size(); }
};

// Block factory for creating appropriate block types
class EnhancedBlockFactory {
public:
    // Create blocks from data
    static EnhancedSSTableBlock create_sstable_block(const std::string& data);
    static EnhancedWALBlock create_wal_block(uint64_t lsn, const std::string& entry);
    static EnhancedMemTableBlock create_memtable_block(const std::vector<uint8_t>& data, uint32_t count);
    
    // Detect block type from serialized data
    static EnhancedBlockHeader::BlockType detect_block_type(const std::vector<uint8_t>& data);
    static EnhancedBlockHeader::BlockType detect_block_type(std::istream& in);
    
    // Validate any block type
    static IntegrityStatus validate_block(const std::vector<uint8_t>& data);
    static IntegrityStatus validate_block(std::istream& in);
};

// Utility functions for block operations
namespace BlockUtils {
    // Convert between text and binary formats
    std::vector<uint8_t> text_to_binary(const std::string& text);
    std::string binary_to_text(const std::vector<uint8_t>& binary);
    
    // Block size calculations
    size_t calculate_block_overhead();
    size_t calculate_max_data_size(size_t max_block_size);
    
    // Compression utilities (for future use)
    std::vector<uint8_t> compress_block_data(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decompress_block_data(const std::vector<uint8_t>& compressed_data);
}