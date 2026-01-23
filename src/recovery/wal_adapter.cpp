#include "wal_adapter.h"
#include <fstream>
#include <sstream>
#include <iostream>

WALAdapter::WALAdapter(const std::string& wal_directory, bool use_segmented)
    : wal_directory_(wal_directory), use_segmented_wal_(use_segmented) {
    
    if (use_segmented_wal_) {
        initialize_segmented_wal();
    }
}

WALAdapter::~WALAdapter() {
    if (segmented_wal_) {
        segmented_wal_->sync_to_disk();
    }
}

void WALAdapter::log_put(const std::string& key, const std::string& value) {
    if (segmented_wal_) {
        segmented_wal_->log_put(key, value);
    }
}

void WALAdapter::log_del(const std::string& key) {
    if (segmented_wal_) {
        segmented_wal_->log_del(key);
    }
}

void WALAdapter::replay(
    const std::function<void(const std::string&, const std::string&)>& on_put,
    const std::function<void(const std::string&)>& on_del
) {
    if (segmented_wal_) {
        segmented_wal_->replay(on_put, on_del);
    }
}

uint64_t WALAdapter::get_current_lsn() const {
    if (segmented_wal_) {
        return segmented_wal_->get_current_lsn();
    }
    return 0;
}

void WALAdapter::flush_to_disk() {
    if (segmented_wal_) {
        segmented_wal_->flush_all_segments();
    }
}

void WALAdapter::sync_to_disk() {
    if (segmented_wal_) {
        segmented_wal_->sync_to_disk();
    }
}

std::vector<WALEntry> WALAdapter::get_entries_since_lsn(uint64_t lsn) {
    if (segmented_wal_) {
        return segmented_wal_->get_entries_since_lsn(lsn);
    }
    return {};
}

std::vector<WALSegment*> WALAdapter::get_segments_for_recovery(uint64_t from_lsn) {
    if (segmented_wal_) {
        return segmented_wal_->get_segments_for_recovery(from_lsn);
    }
    return {};
}

IntegrityStatus WALAdapter::validate_wal() {
    if (segmented_wal_) {
        return segmented_wal_->validate_all_segments();
    }
    return IntegrityStatus::INVALID_FORMAT;
}

SegmentedWAL::ValidationReport WALAdapter::validate_segments() {
    if (segmented_wal_) {
        return segmented_wal_->validate_segments();
    }
    return SegmentedWAL::ValidationReport{};
}

SegmentedWAL::WALStatistics WALAdapter::get_statistics() const {
    if (segmented_wal_) {
        return segmented_wal_->get_statistics();
    }
    return SegmentedWAL::WALStatistics{};
}

void WALAdapter::print_statistics() const {
    if (segmented_wal_) {
        segmented_wal_->print_statistics();
    }
}

void WALAdapter::set_max_segment_size(size_t size) {
    if (segmented_wal_) {
        segmented_wal_->set_max_segment_size(size);
    }
}

void WALAdapter::enable_auto_flush(bool enabled) {
    if (segmented_wal_) {
        segmented_wal_->enable_auto_flush(enabled);
    }
}

bool WALAdapter::migrate_from_legacy_wal(const std::string& legacy_wal_file) {
    if (!segmented_wal_) {
        return false;
    }
    
    std::ifstream file(legacy_wal_file);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    uint64_t lsn = 1;
    
    while (std::getline(file, line)) {
        try {
            WALEntry entry = WALUtils::parse_legacy_entry(line, lsn++);
            segmented_wal_->write_entry(entry);
        } catch (const std::exception& e) {
            std::cerr << "Error migrating WAL entry: " << e.what() << std::endl;
            return false;
        }
    }
    
    segmented_wal_->sync_to_disk();
    return true;
}

bool WALAdapter::export_to_legacy_format(const std::string& output_file) {
    if (!segmented_wal_) {
        return false;
    }
    
    std::ofstream file(output_file);
    if (!file.is_open()) {
        return false;
    }
    
    std::vector<WALEntry> all_entries = segmented_wal_->get_all_entries();
    
    for (const WALEntry& entry : all_entries) {
        std::string legacy_line = WALUtils::format_entry_for_legacy(entry);
        file << legacy_line << "\n";
    }
    
    return file.good();
}

void WALAdapter::initialize_segmented_wal() {
    segmented_wal_ = std::make_unique<SegmentedWAL>(wal_directory_);
}

// Factory function
std::unique_ptr<WALAdapter> create_wal(const std::string& wal_directory, bool use_segmented) {
    return std::make_unique<WALAdapter>(wal_directory, use_segmented);
}

// WALMigration namespace implementation
namespace WALMigration {
    bool migrate_legacy_to_segmented(const std::string& legacy_file, const std::string& segmented_dir) {
        WALAdapter adapter(segmented_dir, true);
        return adapter.migrate_from_legacy_wal(legacy_file);
    }
    
    bool export_segmented_to_legacy(const std::string& segmented_dir, const std::string& legacy_file) {
        WALAdapter adapter(segmented_dir, true);
        return adapter.export_to_legacy_format(legacy_file);
    }
    
    bool validate_migration(const std::string& legacy_file, const std::string& segmented_dir) {
        // Read legacy file
        std::ifstream legacy(legacy_file);
        if (!legacy.is_open()) {
            return false;
        }
        
        std::vector<std::string> legacy_lines;
        std::string line;
        while (std::getline(legacy, line)) {
            legacy_lines.push_back(line);
        }
        
        // Read segmented WAL
        WALAdapter adapter(segmented_dir, true);
        std::vector<WALEntry> segmented_entries = adapter.get_segmented_wal()->get_all_entries();
        
        // Compare counts
        if (legacy_lines.size() != segmented_entries.size()) {
            return false;
        }
        
        // Compare content
        for (size_t i = 0; i < legacy_lines.size(); i++) {
            std::string legacy_formatted = WALUtils::format_entry_for_legacy(segmented_entries[i]);
            if (legacy_lines[i] != legacy_formatted) {
                return false;
            }
        }
        
        return true;
    }
}