#pragma once
#include <vector>
#include <memory>
#include <string>
#include <fstream>
#include <mutex>
#include <cstdint>
#include <functional>
#include "crc_checksum.h"
#include "integrity_checker.h"

// Forward declarations
struct WALEntry;

// WAL Entry structure with CRC32 checksum
struct WALEntry {
    uint64_t lsn;           // Log sequence number
    uint32_t entry_size;    // Size of entry data
    uint32_t crc32;         // CRC32 checksum of entry data
    uint32_t entry_type;    // Type of WAL entry (PUT, DEL, etc.)
    uint64_t timestamp;     // Entry timestamp
    std::vector<uint8_t> data; // Variable-length entry data
    
    enum EntryType {
        PUT = 1,
        DELETE = 2,
        TRANSACTION_BEGIN = 3,
        TRANSACTION_COMMIT = 4,
        TRANSACTION_ABORT = 5
    };
    
    WALEntry() : lsn(0), entry_size(0), crc32(0), entry_type(PUT), timestamp(0) {}
    
    WALEntry(uint64_t sequence_number, EntryType type, const std::vector<uint8_t>& entry_data);
    WALEntry(uint64_t sequence_number, EntryType type, const std::string& entry_data);
    
    // Calculate and set CRC32 for the entry data
    void calculate_crc32();
    
    // Verify the CRC32 checksum
    bool verify_crc32() const;
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    static WALEntry deserialize(const std::vector<uint8_t>& serialized_data);
    static WALEntry deserialize(const uint8_t* data, size_t size);
    
    // Size calculation
    size_t serialized_size() const;
    
    // Legacy format conversion
    static WALEntry from_legacy_format(const std::string& legacy_entry, uint64_t lsn);
};

// WAL Segment header structure
struct SegmentHeader {
    uint32_t magic_number;      // Magic number for format identification
    uint32_t version;           // Segment format version
    uint64_t segment_id;        // Unique segment identifier
    uint64_t start_lsn;         // Starting LSN for this segment
    uint64_t end_lsn;           // Ending LSN for this segment
    uint32_t entry_count;       // Number of entries in this segment
    uint64_t segment_size;      // Total segment size in bytes
    uint64_t creation_time;     // Segment creation timestamp
    uint32_t header_crc32;      // CRC32 of header (excluding this field)
    uint32_t data_crc32;        // CRC32 of all entry data in segment
    uint32_t reserved[6];       // Reserved for future use
    
    static constexpr uint32_t MAGIC_NUMBER = 0x57414C53; // "WALS"
    static constexpr uint32_t CURRENT_VERSION = 1;
    
    SegmentHeader() : magic_number(MAGIC_NUMBER), version(CURRENT_VERSION),
                     segment_id(0), start_lsn(0), end_lsn(0), entry_count(0),
                     segment_size(0), creation_time(0), header_crc32(0), data_crc32(0) {
        memset(reserved, 0, sizeof(reserved));
    }
    
    // Validate header integrity
    bool is_valid() const {
        return magic_number == MAGIC_NUMBER && version <= CURRENT_VERSION;
    }
    
    // Calculate header CRC32 (excluding header_crc32 field)
    uint32_t calculate_header_crc32() const;
    
    // Verify header CRC32
    bool verify_header_crc32() const;
    
    // Update header with current data
    void update_header_crc32();
};

// Individual WAL segment
class WALSegment {
private:
    SegmentHeader header_;
    std::vector<WALEntry> entries_;
    std::string segment_file_path_;
    std::unique_ptr<std::fstream> file_stream_;
    mutable std::mutex segment_mutex_;
    bool is_sealed_;
    
public:
    static constexpr size_t MAX_SEGMENT_SIZE = 64 * 1024 * 1024; // 64MB
    
    WALSegment(uint64_t segment_id, const std::string& segment_dir);
    ~WALSegment();
    
    // Entry management
    bool add_entry(const WALEntry& entry);
    bool can_add_entry(const WALEntry& entry) const;
    
    // Segment operations
    void seal_segment();
    bool is_sealed() const { return is_sealed_; }
    void flush_to_disk();
    
    // Access methods
    uint64_t get_segment_id() const { return header_.segment_id; }
    uint64_t get_start_lsn() const { return header_.start_lsn; }
    uint64_t get_end_lsn() const { return header_.end_lsn; }
    uint32_t get_entry_count() const { return header_.entry_count; }
    uint64_t get_segment_size() const { return header_.segment_size; }
    const std::string& get_file_path() const { return segment_file_path_; }
    
    // Entry iteration
    const std::vector<WALEntry>& get_entries() const { return entries_; }
    std::vector<WALEntry> get_entries_in_lsn_range(uint64_t start_lsn, uint64_t end_lsn) const;
    
    // Integrity operations
    IntegrityStatus validate_segment() const;
    bool verify_all_entries() const;
    void calculate_data_crc32();
    
    // Serialization
    std::vector<uint8_t> serialize_header() const;
    bool load_from_file();
    bool save_to_file();
    
    // Recovery support
    std::vector<WALEntry> read_entries_for_recovery() const;
    
private:
    void initialize_file();
    void update_header();
    bool write_entry_to_file(const WALEntry& entry);
    bool read_entries_from_file();
};

// Segmented WAL manager
class SegmentedWAL {
private:
    std::vector<std::unique_ptr<WALSegment>> segments_;
    std::string wal_directory_;
    uint64_t current_lsn_;
    uint64_t next_segment_id_;
    WALSegment* current_segment_;
    mutable std::mutex wal_mutex_;
    
    // Configuration
    size_t max_segment_size_;
    uint32_t max_segments_in_memory_;
    bool auto_flush_enabled_;
    
public:
    SegmentedWAL(const std::string& wal_directory);
    ~SegmentedWAL();
    
    // Configuration
    void set_max_segment_size(size_t size) { max_segment_size_ = size; }
    void set_max_segments_in_memory(uint32_t count) { max_segments_in_memory_ = count; }
    void enable_auto_flush(bool enabled) { auto_flush_enabled_ = enabled; }
    
    // Entry operations
    uint64_t write_entry(WALEntry::EntryType type, const std::string& key, const std::string& value);
    uint64_t write_entry(WALEntry::EntryType type, const std::vector<uint8_t>& data);
    uint64_t write_entry(const WALEntry& entry);
    
    // Segment management
    WALSegment* create_new_segment();
    void seal_current_segment();
    void cleanup_old_segments(uint64_t min_lsn_to_keep);
    
    // Recovery operations
    std::vector<WALSegment*> get_segments_for_recovery(uint64_t from_lsn = 0);
    std::vector<WALEntry> get_entries_since_lsn(uint64_t lsn);
    std::vector<WALEntry> get_all_entries();
    
    // Access methods
    uint64_t get_current_lsn() const { return current_lsn_; }
    uint64_t get_next_lsn() { return ++current_lsn_; }
    size_t get_segment_count() const { return segments_.size(); }
    
    // Integrity operations
    IntegrityStatus validate_all_segments() const;
    struct ValidationReport {
        size_t total_segments;
        size_t valid_segments;
        size_t corrupted_segments;
        std::vector<uint64_t> corrupted_segment_ids;
        double integrity_rate;
        
        ValidationReport() : total_segments(0), valid_segments(0), 
                           corrupted_segments(0), integrity_rate(0.0) {}
    };
    ValidationReport validate_segments() const;
    
    // File operations
    void flush_all_segments();
    bool load_existing_segments();
    void sync_to_disk();
    
    // Compatibility with existing WAL interface
    void log_put(const std::string& key, const std::string& value);
    void log_del(const std::string& key);
    void replay(
        const std::function<void(const std::string&, const std::string&)>& on_put,
        const std::function<void(const std::string&)>& on_del
    );
    
    // Statistics and monitoring
    struct WALStatistics {
        uint64_t total_entries;
        uint64_t total_segments;
        uint64_t total_size_bytes;
        uint64_t current_lsn;
        double average_entries_per_segment;
        double average_segment_size;
        
        WALStatistics() : total_entries(0), total_segments(0), total_size_bytes(0),
                         current_lsn(0), average_entries_per_segment(0.0), average_segment_size(0.0) {}
    };
    WALStatistics get_statistics() const;
    void print_statistics() const;
    
private:
    void initialize_wal_directory();
    void load_wal_state();
    void save_wal_state();
    std::string generate_segment_filename(uint64_t segment_id) const;
    void cleanup_memory_segments();
    WALEntry create_wal_entry(WALEntry::EntryType type, const std::string& key, const std::string& value);
    WALEntry create_wal_entry(WALEntry::EntryType type, const std::vector<uint8_t>& data);
};

// Utility functions for WAL operations
namespace WALUtils {
    // LSN management
    bool is_valid_lsn(uint64_t lsn);
    uint64_t get_segment_id_for_lsn(uint64_t lsn, size_t entries_per_segment);
    
    // Entry parsing for legacy compatibility
    WALEntry parse_legacy_entry(const std::string& line, uint64_t lsn);
    std::string format_entry_for_legacy(const WALEntry& entry);
    
    // File utilities
    std::vector<std::string> find_wal_segment_files(const std::string& directory);
    bool is_wal_segment_file(const std::string& filename);
    uint64_t extract_segment_id_from_filename(const std::string& filename);
    
    // Recovery utilities
    std::vector<WALEntry> merge_entries_from_segments(const std::vector<WALSegment*>& segments);
    void sort_entries_by_lsn(std::vector<WALEntry>& entries);
}