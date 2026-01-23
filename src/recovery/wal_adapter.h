#pragma once
#include "segmented_wal.h"
#include "../log/wal.h"
#include <memory>

// Adapter class to integrate SegmentedWAL with existing WAL interface
class WALAdapter {
private:
    std::unique_ptr<SegmentedWAL> segmented_wal_;
    std::string wal_directory_;
    bool use_segmented_wal_;
    
public:
    WALAdapter(const std::string& wal_directory, bool use_segmented = true);
    ~WALAdapter();
    
    // WAL operations compatible with existing interface
    void log_put(const std::string& key, const std::string& value);
    void log_del(const std::string& key);
    
    void replay(
        const std::function<void(const std::string&, const std::string&)>& on_put,
        const std::function<void(const std::string&)>& on_del
    );
    
    // Enhanced operations available with segmented WAL
    uint64_t get_current_lsn() const;
    void flush_to_disk();
    void sync_to_disk();
    
    // Recovery operations
    std::vector<WALEntry> get_entries_since_lsn(uint64_t lsn);
    std::vector<WALSegment*> get_segments_for_recovery(uint64_t from_lsn = 0);
    
    // Integrity operations
    IntegrityStatus validate_wal();
    SegmentedWAL::ValidationReport validate_segments();
    
    // Statistics
    SegmentedWAL::WALStatistics get_statistics() const;
    void print_statistics() const;
    
    // Configuration
    void set_max_segment_size(size_t size);
    void enable_auto_flush(bool enabled);
    
    // Migration utilities
    bool migrate_from_legacy_wal(const std::string& legacy_wal_file);
    bool export_to_legacy_format(const std::string& output_file);
    
    // Access to underlying segmented WAL
    SegmentedWAL* get_segmented_wal() { return segmented_wal_.get(); }
    const SegmentedWAL* get_segmented_wal() const { return segmented_wal_.get(); }
    
private:
    void initialize_segmented_wal();
};

// Factory function to create WAL instances
std::unique_ptr<WALAdapter> create_wal(const std::string& wal_directory, bool use_segmented = true);

// Migration utilities
namespace WALMigration {
    // Convert legacy WAL file to segmented format
    bool migrate_legacy_to_segmented(const std::string& legacy_file, const std::string& segmented_dir);
    
    // Convert segmented WAL to legacy format
    bool export_segmented_to_legacy(const std::string& segmented_dir, const std::string& legacy_file);
    
    // Validate migration integrity
    bool validate_migration(const std::string& legacy_file, const std::string& segmented_dir);
}