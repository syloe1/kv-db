#include "segmented_wal.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <functional>

namespace fs = std::filesystem;

// WALEntry implementation
WALEntry::WALEntry(uint64_t sequence_number, EntryType type, const std::vector<uint8_t>& entry_data)
    : lsn(sequence_number), entry_type(static_cast<uint32_t>(type)), data(entry_data) {
    entry_size = static_cast<uint32_t>(data.size());
    timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    calculate_crc32();
}

WALEntry::WALEntry(uint64_t sequence_number, EntryType type, const std::string& entry_data)
    : lsn(sequence_number), entry_type(static_cast<uint32_t>(type)) {
    data.assign(entry_data.begin(), entry_data.end());
    entry_size = static_cast<uint32_t>(data.size());
    timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    calculate_crc32();
}

void WALEntry::calculate_crc32() {
    crc32 = CRC32::calculate(data.data(), data.size());
}

bool WALEntry::verify_crc32() const {
    return CRC32::verify(data.data(), data.size(), crc32);
}

std::vector<uint8_t> WALEntry::serialize() const {
    std::vector<uint8_t> result;
    
    // Calculate total size: fixed fields + data
    size_t total_size = sizeof(uint64_t) + sizeof(uint32_t) * 3 + sizeof(uint64_t) + data.size();
    result.resize(total_size);
    
    size_t offset = 0;
    
    // Serialize LSN
    memcpy(result.data() + offset, &lsn, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    // Serialize entry_size
    memcpy(result.data() + offset, &entry_size, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Serialize crc32
    memcpy(result.data() + offset, &crc32, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Serialize entry_type
    memcpy(result.data() + offset, &entry_type, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Serialize timestamp
    memcpy(result.data() + offset, &timestamp, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    // Serialize data
    if (!data.empty()) {
        memcpy(result.data() + offset, data.data(), data.size());
    }
    
    return result;
}

WALEntry WALEntry::deserialize(const std::vector<uint8_t>& serialized_data) {
    return deserialize(serialized_data.data(), serialized_data.size());
}

WALEntry WALEntry::deserialize(const uint8_t* data, size_t size) {
    if (size < sizeof(uint64_t) + sizeof(uint32_t) * 3 + sizeof(uint64_t)) {
        throw std::runtime_error("Invalid WAL entry data: too small");
    }
    
    WALEntry entry;
    size_t offset = 0;
    
    // Deserialize LSN
    memcpy(&entry.lsn, data + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    // Deserialize entry_size
    memcpy(&entry.entry_size, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Deserialize crc32
    memcpy(&entry.crc32, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Deserialize entry_type
    memcpy(&entry.entry_type, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Deserialize timestamp
    memcpy(&entry.timestamp, data + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    // Deserialize entry data
    if (entry.entry_size > 0) {
        if (size < offset + entry.entry_size) {
            throw std::runtime_error("Invalid WAL entry data: data size mismatch");
        }
        entry.data.resize(entry.entry_size);
        memcpy(entry.data.data(), data + offset, entry.entry_size);
    }
    
    return entry;
}

size_t WALEntry::serialized_size() const {
    return sizeof(uint64_t) + sizeof(uint32_t) * 3 + sizeof(uint64_t) + data.size();
}

WALEntry WALEntry::from_legacy_format(const std::string& legacy_entry, uint64_t lsn) {
    std::istringstream iss(legacy_entry);
    std::string cmd;
    iss >> cmd;
    
    if (cmd == "PUT") {
        std::string key, value;
        iss >> key >> value;
        std::string combined = key + " " + value;
        return WALEntry(lsn, PUT, combined);
    } else if (cmd == "DEL") {
        std::string key;
        iss >> key;
        return WALEntry(lsn, DELETE, key);
    } else {
        throw std::runtime_error("Unknown legacy WAL command: " + cmd);
    }
}

// SegmentHeader implementation
uint32_t SegmentHeader::calculate_header_crc32() const {
    // Calculate CRC32 of header excluding header_crc32 field
    struct HeaderForCRC {
        uint32_t magic_number;
        uint32_t version;
        uint64_t segment_id;
        uint64_t start_lsn;
        uint64_t end_lsn;
        uint32_t entry_count;
        uint64_t segment_size;
        uint64_t creation_time;
        uint32_t data_crc32;
        uint32_t reserved[6];
    } header_for_crc;
    
    header_for_crc.magic_number = magic_number;
    header_for_crc.version = version;
    header_for_crc.segment_id = segment_id;
    header_for_crc.start_lsn = start_lsn;
    header_for_crc.end_lsn = end_lsn;
    header_for_crc.entry_count = entry_count;
    header_for_crc.segment_size = segment_size;
    header_for_crc.creation_time = creation_time;
    header_for_crc.data_crc32 = data_crc32;
    memcpy(header_for_crc.reserved, reserved, sizeof(reserved));
    
    return CRC32::calculate(&header_for_crc, sizeof(header_for_crc));
}

bool SegmentHeader::verify_header_crc32() const {
    uint32_t calculated = calculate_header_crc32();
    std::cout << "Header CRC32 - Stored: " << header_crc32 << ", Calculated: " << calculated << std::endl;
    return calculated == header_crc32;
}

void SegmentHeader::update_header_crc32() {
    header_crc32 = calculate_header_crc32();
}

// WALSegment implementation
WALSegment::WALSegment(uint64_t segment_id, const std::string& segment_dir)
    : is_sealed_(false) {
    header_.segment_id = segment_id;
    header_.creation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Create segment file path
    segment_file_path_ = segment_dir + "/wal_segment_" + std::to_string(segment_id) + ".seg";
    
    initialize_file();
}

WALSegment::~WALSegment() {
    if (file_stream_ && file_stream_->is_open()) {
        flush_to_disk();
        file_stream_->close();
    }
}

bool WALSegment::add_entry(const WALEntry& entry) {
    std::lock_guard<std::mutex> lock(segment_mutex_);
    
    if (is_sealed_) {
        return false;
    }
    
    if (!can_add_entry(entry)) {
        return false;
    }
    
    // Update segment header
    if (entries_.empty()) {
        header_.start_lsn = entry.lsn;
    }
    header_.end_lsn = entry.lsn;
    header_.entry_count++;
    header_.segment_size += entry.serialized_size();
    
    // Add entry to memory
    entries_.push_back(entry);
    
    // Write to file
    if (!write_entry_to_file(entry)) {
        // Rollback on failure
        entries_.pop_back();
        header_.entry_count--;
        header_.segment_size -= entry.serialized_size();
        if (entries_.empty()) {
            header_.start_lsn = 0;
            header_.end_lsn = 0;
        } else {
            header_.end_lsn = entries_.back().lsn;
        }
        return false;
    }
    
    return true;
}

bool WALSegment::can_add_entry(const WALEntry& entry) const {
    if (is_sealed_) {
        return false;
    }
    
    size_t entry_size = entry.serialized_size();
    size_t current_size = header_.segment_size + sizeof(SegmentHeader);
    
    return (current_size + entry_size) <= MAX_SEGMENT_SIZE;
}

void WALSegment::seal_segment() {
    std::lock_guard<std::mutex> lock(segment_mutex_);
    
    if (is_sealed_) {
        return;
    }
    
    is_sealed_ = true;
    calculate_data_crc32();
    header_.update_header_crc32();  // This should calculate and set the header CRC32
    update_header();
    flush_to_disk();
}

void WALSegment::flush_to_disk() {
    if (file_stream_) {
        file_stream_->flush();
        file_stream_->sync();
    }
}

std::vector<WALEntry> WALSegment::get_entries_in_lsn_range(uint64_t start_lsn, uint64_t end_lsn) const {
    std::lock_guard<std::mutex> lock(segment_mutex_);
    
    std::vector<WALEntry> result;
    for (const auto& entry : entries_) {
        if (entry.lsn >= start_lsn && entry.lsn <= end_lsn) {
            result.push_back(entry);
        }
    }
    
    return result;
}

IntegrityStatus WALSegment::validate_segment() const {
    std::lock_guard<std::mutex> lock(segment_mutex_);
    
    // Validate header
    if (!header_.is_valid()) {
        std::cout << "Header invalid: magic=" << std::hex << header_.magic_number 
                  << ", version=" << header_.version << std::endl;
        return IntegrityStatus::INVALID_FORMAT;
    }
    
    if (!header_.verify_header_crc32()) {
        std::cout << "Header CRC32 verification failed" << std::endl;
        return IntegrityStatus::CORRUPTION_DETECTED;
    }
    
    // Validate all entries
    if (!verify_all_entries()) {
        std::cout << "Entry verification failed" << std::endl;
        return IntegrityStatus::CORRUPTION_DETECTED;
    }
    
    return IntegrityStatus::OK;
}

bool WALSegment::verify_all_entries() const {
    for (const auto& entry : entries_) {
        if (!entry.verify_crc32()) {
            return false;
        }
    }
    return true;
}

void WALSegment::calculate_data_crc32() {
    if (entries_.empty()) {
        header_.data_crc32 = 0;
        return;
    }
    
    // Calculate CRC32 of all serialized entries
    std::vector<uint8_t> all_data;
    for (const auto& entry : entries_) {
        auto serialized = entry.serialize();
        all_data.insert(all_data.end(), serialized.begin(), serialized.end());
    }
    
    header_.data_crc32 = CRC32::calculate(all_data.data(), all_data.size());
}

std::vector<uint8_t> WALSegment::serialize_header() const {
    std::vector<uint8_t> result(sizeof(SegmentHeader));
    memcpy(result.data(), &header_, sizeof(SegmentHeader));
    return result;
}

bool WALSegment::load_from_file() {
    std::lock_guard<std::mutex> lock(segment_mutex_);
    
    if (!fs::exists(segment_file_path_)) {
        return false;
    }
    
    std::ifstream file(segment_file_path_, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read header
    file.read(reinterpret_cast<char*>(&header_), sizeof(SegmentHeader));
    if (file.gcount() != sizeof(SegmentHeader)) {
        return false;
    }
    
    // Validate header
    if (!header_.is_valid() || !header_.verify_header_crc32()) {
        return false;
    }
    
    // Read entries
    entries_.clear();
    entries_.reserve(header_.entry_count);
    
    for (uint32_t i = 0; i < header_.entry_count; i++) {
        // Read entry header first to get size
        uint64_t lsn;
        uint32_t entry_size, crc32, entry_type;
        uint64_t timestamp;
        
        file.read(reinterpret_cast<char*>(&lsn), sizeof(uint64_t));
        file.read(reinterpret_cast<char*>(&entry_size), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&crc32), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&entry_type), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&timestamp), sizeof(uint64_t));
        
        if (file.fail()) {
            return false;
        }
        
        // Read entry data
        std::vector<uint8_t> entry_data(entry_size);
        if (entry_size > 0) {
            file.read(reinterpret_cast<char*>(entry_data.data()), entry_size);
            if (file.gcount() != static_cast<std::streamsize>(entry_size)) {
                return false;
            }
        }
        
        // Create entry
        WALEntry entry;
        entry.lsn = lsn;
        entry.entry_size = entry_size;
        entry.crc32 = crc32;
        entry.entry_type = entry_type;
        entry.timestamp = timestamp;
        entry.data = std::move(entry_data);
        
        // Verify entry integrity
        if (!entry.verify_crc32()) {
            return false;
        }
        
        entries_.push_back(std::move(entry));
    }
    
    return true;
}

bool WALSegment::save_to_file() {
    std::lock_guard<std::mutex> lock(segment_mutex_);
    
    std::ofstream file(segment_file_path_, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    
    // Update and write header
    update_header();
    file.write(reinterpret_cast<const char*>(&header_), sizeof(SegmentHeader));
    
    // Write all entries
    for (const auto& entry : entries_) {
        auto serialized = entry.serialize();
        file.write(reinterpret_cast<const char*>(serialized.data()), serialized.size());
    }
    
    file.flush();
    return file.good();
}

std::vector<WALEntry> WALSegment::read_entries_for_recovery() const {
    std::lock_guard<std::mutex> lock(segment_mutex_);
    return entries_;
}

void WALSegment::initialize_file() {
    // Create directory if it doesn't exist
    fs::path file_path(segment_file_path_);
    fs::create_directories(file_path.parent_path());
    
    // Open file for read/write
    file_stream_ = std::make_unique<std::fstream>(
        segment_file_path_, 
        std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc
    );
    
    if (!file_stream_->is_open()) {
        throw std::runtime_error("Failed to open WAL segment file: " + segment_file_path_);
    }
    
    // Write initial header (will be updated when segment is sealed)
    header_.update_header_crc32();
    file_stream_->write(reinterpret_cast<const char*>(&header_), sizeof(SegmentHeader));
    file_stream_->flush();
}

void WALSegment::update_header() {
    header_.update_header_crc32();  // Make sure CRC32 is calculated
    
    if (file_stream_ && file_stream_->is_open()) {
        file_stream_->seekp(0);
        file_stream_->write(reinterpret_cast<const char*>(&header_), sizeof(SegmentHeader));
        file_stream_->flush();
    }
}

bool WALSegment::write_entry_to_file(const WALEntry& entry) {
    if (!file_stream_ || !file_stream_->is_open()) {
        return false;
    }
    
    // Seek to end of file
    file_stream_->seekp(0, std::ios::end);
    
    // Write serialized entry
    auto serialized = entry.serialize();
    file_stream_->write(reinterpret_cast<const char*>(serialized.data()), serialized.size());
    
    return file_stream_->good();
}

bool WALSegment::read_entries_from_file() {
    return load_from_file();
}
// SegmentedWAL implementation
SegmentedWAL::SegmentedWAL(const std::string& wal_directory)
    : wal_directory_(wal_directory), current_lsn_(0), next_segment_id_(1),
      current_segment_(nullptr), max_segment_size_(WALSegment::MAX_SEGMENT_SIZE),
      max_segments_in_memory_(10), auto_flush_enabled_(true) {
    
    initialize_wal_directory();
    load_existing_segments();
    
    // Create initial segment if none exist
    if (segments_.empty()) {
        current_segment_ = create_new_segment();
    } else {
        // Use the last segment as current if it's not sealed
        current_segment_ = segments_.back().get();
        if (current_segment_->is_sealed()) {
            current_segment_ = create_new_segment();
        }
    }
}

SegmentedWAL::~SegmentedWAL() {
    flush_all_segments();
    save_wal_state();
}

uint64_t SegmentedWAL::write_entry(WALEntry::EntryType type, const std::string& key, const std::string& value) {
    WALEntry entry = create_wal_entry(type, key, value);
    return write_entry(entry);
}

uint64_t SegmentedWAL::write_entry(WALEntry::EntryType type, const std::vector<uint8_t>& data) {
    WALEntry entry = create_wal_entry(type, data);
    return write_entry(entry);
}

uint64_t SegmentedWAL::write_entry(const WALEntry& entry) {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    
    // Assign LSN if not already set
    WALEntry mutable_entry = entry;
    if (mutable_entry.lsn == 0) {
        mutable_entry.lsn = get_next_lsn();
    }
    
    // Check if current segment can accommodate the entry
    if (!current_segment_ || !current_segment_->can_add_entry(mutable_entry)) {
        // Seal current segment and create new one
        if (current_segment_) {
            current_segment_->seal_segment();
        }
        current_segment_ = create_new_segment();
    }
    
    // Add entry to current segment
    if (!current_segment_->add_entry(mutable_entry)) {
        throw std::runtime_error("Failed to add entry to WAL segment");
    }
    
    // Auto-flush if enabled
    if (auto_flush_enabled_) {
        current_segment_->flush_to_disk();
    }
    
    // Cleanup old segments if we have too many in memory
    cleanup_memory_segments();
    
    return mutable_entry.lsn;
}

WALSegment* SegmentedWAL::create_new_segment() {
    uint64_t segment_id = next_segment_id_++;
    auto segment = std::make_unique<WALSegment>(segment_id, wal_directory_);
    WALSegment* segment_ptr = segment.get();
    segments_.push_back(std::move(segment));
    return segment_ptr;
}

void SegmentedWAL::seal_current_segment() {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    if (current_segment_) {
        current_segment_->seal_segment();
        current_segment_ = create_new_segment();
    }
}

void SegmentedWAL::cleanup_old_segments(uint64_t min_lsn_to_keep) {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    
    auto it = segments_.begin();
    while (it != segments_.end()) {
        if ((*it)->get_end_lsn() < min_lsn_to_keep && (*it)->is_sealed()) {
            // Remove segment file
            fs::remove((*it)->get_file_path());
            it = segments_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<WALSegment*> SegmentedWAL::get_segments_for_recovery(uint64_t from_lsn) {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    
    std::vector<WALSegment*> result;
    for (const auto& segment : segments_) {
        if (segment->get_end_lsn() >= from_lsn) {
            result.push_back(segment.get());
        }
    }
    
    return result;
}

std::vector<WALEntry> SegmentedWAL::get_entries_since_lsn(uint64_t lsn) {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    
    std::vector<WALEntry> result;
    for (const auto& segment : segments_) {
        if (segment->get_end_lsn() >= lsn) {
            auto entries = segment->get_entries_in_lsn_range(lsn, UINT64_MAX);
            result.insert(result.end(), entries.begin(), entries.end());
        }
    }
    
    // Sort by LSN
    std::sort(result.begin(), result.end(), 
              [](const WALEntry& a, const WALEntry& b) { return a.lsn < b.lsn; });
    
    return result;
}

std::vector<WALEntry> SegmentedWAL::get_all_entries() {
    return get_entries_since_lsn(0);
}
IntegrityStatus SegmentedWAL::validate_all_segments() const {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    
    for (const auto& segment : segments_) {
        IntegrityStatus status = segment->validate_segment();
        if (status != IntegrityStatus::OK) {
            return status;
        }
    }
    
    return IntegrityStatus::OK;
}

SegmentedWAL::ValidationReport SegmentedWAL::validate_segments() const {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    
    ValidationReport report;
    report.total_segments = segments_.size();
    
    for (const auto& segment : segments_) {
        IntegrityStatus status = segment->validate_segment();
        if (status == IntegrityStatus::OK) {
            report.valid_segments++;
        } else {
            report.corrupted_segments++;
            report.corrupted_segment_ids.push_back(segment->get_segment_id());
        }
    }
    
    report.integrity_rate = report.total_segments > 0 ? 
        static_cast<double>(report.valid_segments) / report.total_segments : 0.0;
    
    return report;
}

void SegmentedWAL::flush_all_segments() {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    
    for (const auto& segment : segments_) {
        segment->flush_to_disk();
    }
}

bool SegmentedWAL::load_existing_segments() {
    if (!fs::exists(wal_directory_)) {
        return true; // No existing segments
    }
    
    std::vector<std::string> segment_files = WALUtils::find_wal_segment_files(wal_directory_);
    
    // Sort by segment ID
    std::sort(segment_files.begin(), segment_files.end(), 
              [](const std::string& a, const std::string& b) {
                  return WALUtils::extract_segment_id_from_filename(a) < 
                         WALUtils::extract_segment_id_from_filename(b);
              });
    
    for (const std::string& file_path : segment_files) {
        uint64_t segment_id = WALUtils::extract_segment_id_from_filename(file_path);
        auto segment = std::make_unique<WALSegment>(segment_id, wal_directory_);
        
        if (segment->load_from_file()) {
            // Update current LSN and next segment ID
            current_lsn_ = std::max(current_lsn_, segment->get_end_lsn());
            next_segment_id_ = std::max(next_segment_id_, segment_id + 1);
            
            segments_.push_back(std::move(segment));
        }
    }
    
    return true;
}

void SegmentedWAL::sync_to_disk() {
    flush_all_segments();
    save_wal_state();
}

// Compatibility methods with existing WAL interface
void SegmentedWAL::log_put(const std::string& key, const std::string& value) {
    write_entry(WALEntry::PUT, key, value);
}

void SegmentedWAL::log_del(const std::string& key) {
    write_entry(WALEntry::DELETE, key, "");
}

void SegmentedWAL::replay(
    const std::function<void(const std::string&, const std::string&)>& on_put,
    const std::function<void(const std::string&)>& on_del
) {
    std::vector<WALEntry> all_entries = get_all_entries();
    
    for (const WALEntry& entry : all_entries) {
        std::string data_str(entry.data.begin(), entry.data.end());
        
        if (entry.entry_type == WALEntry::PUT) {
            // Parse "key value" format
            std::istringstream iss(data_str);
            std::string key, value;
            iss >> key >> value;
            on_put(key, value);
        } else if (entry.entry_type == WALEntry::DELETE) {
            // Remove any trailing whitespace
            while (!data_str.empty() && std::isspace(data_str.back())) {
                data_str.pop_back();
            }
            on_del(data_str);
        }
    }
}

SegmentedWAL::WALStatistics SegmentedWAL::get_statistics() const {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    
    WALStatistics stats;
    stats.total_segments = segments_.size();
    stats.current_lsn = current_lsn_;
    
    for (const auto& segment : segments_) {
        stats.total_entries += segment->get_entry_count();
        stats.total_size_bytes += segment->get_segment_size();
    }
    
    if (stats.total_segments > 0) {
        stats.average_entries_per_segment = 
            static_cast<double>(stats.total_entries) / stats.total_segments;
        stats.average_segment_size = 
            static_cast<double>(stats.total_size_bytes) / stats.total_segments;
    }
    
    return stats;
}

void SegmentedWAL::print_statistics() const {
    WALStatistics stats = get_statistics();
    
    std::cout << "=== WAL Statistics ===" << std::endl;
    std::cout << "Total segments: " << stats.total_segments << std::endl;
    std::cout << "Total entries: " << stats.total_entries << std::endl;
    std::cout << "Total size: " << stats.total_size_bytes << " bytes" << std::endl;
    std::cout << "Current LSN: " << stats.current_lsn << std::endl;
    std::cout << "Average entries per segment: " << std::fixed << std::setprecision(2) 
              << stats.average_entries_per_segment << std::endl;
    std::cout << "Average segment size: " << std::fixed << std::setprecision(2) 
              << stats.average_segment_size << " bytes" << std::endl;
    std::cout << "======================" << std::endl;
}
// Private helper methods
void SegmentedWAL::initialize_wal_directory() {
    if (!fs::exists(wal_directory_)) {
        fs::create_directories(wal_directory_);
    }
}

void SegmentedWAL::load_wal_state() {
    // Load WAL metadata if exists
    std::string state_file = wal_directory_ + "/wal_state.meta";
    if (fs::exists(state_file)) {
        std::ifstream file(state_file, std::ios::binary);
        if (file.is_open()) {
            file.read(reinterpret_cast<char*>(&current_lsn_), sizeof(uint64_t));
            file.read(reinterpret_cast<char*>(&next_segment_id_), sizeof(uint64_t));
        }
    }
}

void SegmentedWAL::save_wal_state() {
    std::string state_file = wal_directory_ + "/wal_state.meta";
    std::ofstream file(state_file, std::ios::binary | std::ios::trunc);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(&current_lsn_), sizeof(uint64_t));
        file.write(reinterpret_cast<const char*>(&next_segment_id_), sizeof(uint64_t));
    }
}

std::string SegmentedWAL::generate_segment_filename(uint64_t segment_id) const {
    return wal_directory_ + "/wal_segment_" + std::to_string(segment_id) + ".seg";
}

void SegmentedWAL::cleanup_memory_segments() {
    if (segments_.size() <= max_segments_in_memory_) {
        return;
    }
    
    // Keep only the most recent segments in memory
    size_t segments_to_remove = segments_.size() - max_segments_in_memory_;
    
    for (size_t i = 0; i < segments_to_remove; i++) {
        if (segments_[i]->is_sealed() && segments_[i].get() != current_segment_) {
            // Save to disk before removing from memory
            segments_[i]->save_to_file();
        }
    }
    
    // Remove old segments from memory (but keep files on disk)
    segments_.erase(segments_.begin(), segments_.begin() + segments_to_remove);
}

WALEntry SegmentedWAL::create_wal_entry(WALEntry::EntryType type, const std::string& key, const std::string& value) {
    std::string combined_data = key + " " + value;
    return WALEntry(0, type, combined_data); // LSN will be assigned later
}

WALEntry SegmentedWAL::create_wal_entry(WALEntry::EntryType type, const std::vector<uint8_t>& data) {
    return WALEntry(0, type, data); // LSN will be assigned later
}

// WALUtils namespace implementation
namespace WALUtils {
    bool is_valid_lsn(uint64_t lsn) {
        return lsn > 0;
    }
    
    uint64_t get_segment_id_for_lsn(uint64_t lsn, size_t entries_per_segment) {
        if (entries_per_segment == 0) {
            return 1;
        }
        return (lsn - 1) / entries_per_segment + 1;
    }
    
    WALEntry parse_legacy_entry(const std::string& line, uint64_t lsn) {
        return WALEntry::from_legacy_format(line, lsn);
    }
    
    std::string format_entry_for_legacy(const WALEntry& entry) {
        std::string data_str(entry.data.begin(), entry.data.end());
        
        if (entry.entry_type == WALEntry::PUT) {
            return "PUT " + data_str;
        } else if (entry.entry_type == WALEntry::DELETE) {
            return "DEL " + data_str;
        } else {
            return "UNKNOWN " + data_str;
        }
    }
    
    std::vector<std::string> find_wal_segment_files(const std::string& directory) {
        std::vector<std::string> result;
        
        if (!fs::exists(directory)) {
            return result;
        }
        
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file() && is_wal_segment_file(entry.path().filename().string())) {
                result.push_back(entry.path().string());
            }
        }
        
        return result;
    }
    
    bool is_wal_segment_file(const std::string& filename) {
        return filename.find("wal_segment_") == 0 && 
               filename.size() >= 4 && 
               filename.substr(filename.size() - 4) == ".seg";
    }
    
    uint64_t extract_segment_id_from_filename(const std::string& filename) {
        // Extract segment ID from "wal_segment_123.seg" format
        size_t start = filename.find("wal_segment_") + 12;
        size_t end = filename.find(".seg");
        
        if (start == std::string::npos || end == std::string::npos || start >= end) {
            return 0;
        }
        
        std::string id_str = filename.substr(start, end - start);
        try {
            return std::stoull(id_str);
        } catch (const std::exception&) {
            return 0;
        }
    }
    
    std::vector<WALEntry> merge_entries_from_segments(const std::vector<WALSegment*>& segments) {
        std::vector<WALEntry> result;
        
        for (WALSegment* segment : segments) {
            auto entries = segment->read_entries_for_recovery();
            result.insert(result.end(), entries.begin(), entries.end());
        }
        
        sort_entries_by_lsn(result);
        return result;
    }
    
    void sort_entries_by_lsn(std::vector<WALEntry>& entries) {
        std::sort(entries.begin(), entries.end(), 
                  [](const WALEntry& a, const WALEntry& b) { return a.lsn < b.lsn; });
    }
}