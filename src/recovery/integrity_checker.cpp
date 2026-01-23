#include "integrity_checker.h"
#include "crc_checksum.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <filesystem>

// Block implementation
void Block::calculate_crc32() {
    timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    crc32 = CRC32::calculate(data, size);
}

bool Block::verify_crc32() const {
    return CRC32::verify(data, size, crc32);
}

void Block::set_data(const void* src, uint32_t data_size) {
    if (data_size > sizeof(data)) {
        data_size = sizeof(data);
    }
    size = data_size;
    memcpy(data, src, data_size);
    calculate_crc32();
}

std::string Block::get_data_as_string() const {
    return std::string(data, size);
}

// MemTableBlock implementation
void MemTableBlock::calculate_crc32() {
    timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    crc32 = CRC32::calculate(entries, block_size);
}

bool MemTableBlock::verify_crc32() const {
    return CRC32::verify(entries, block_size, crc32);
}

void MemTableBlock::set_entries(const void* entry_data, uint32_t data_size, uint32_t count) {
    if (data_size > sizeof(entries)) {
        data_size = sizeof(entries);
    }
    block_size = data_size;
    entry_count = count;
    memcpy(entries, entry_data, data_size);
    calculate_crc32();
}

// WALBlock implementation
void WALBlock::calculate_crc32() {
    timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    crc32 = CRC32::calculate(entry_data, entry_size);
}

bool WALBlock::verify_crc32() const {
    return CRC32::verify(entry_data, entry_size, crc32);
}

void WALBlock::set_entry_data(const void* data, uint32_t size, uint64_t sequence_number) {
    if (size > sizeof(entry_data)) {
        size = sizeof(entry_data);
    }
    entry_size = size;
    lsn = sequence_number;
    memcpy(entry_data, data, size);
    calculate_crc32();
}

// IntegrityChecker implementation
uint32_t IntegrityChecker::CalculateCRC32(const void* data, size_t size) {
    return CRC32::calculate(data, size);
}

uint32_t IntegrityChecker::CalculateCRC32(const std::string& data) {
    return CRC32::calculate(data);
}

uint32_t IntegrityChecker::CalculateCRC32(const std::vector<uint8_t>& data) {
    return CRC32::calculate(data);
}

bool IntegrityChecker::VerifyCRC32(const Block& block) {
    return block.verify_crc32();
}

IntegrityStatus IntegrityChecker::VerifyBlock(const Block& block) {
    if (block.verify_crc32()) {
        return IntegrityStatus::OK;
    } else {
        return IntegrityStatus::CORRUPTION_DETECTED;
    }
}

IntegrityStatus IntegrityChecker::ValidateFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return IntegrityStatus::FILE_NOT_FOUND;
    }
    
    // Check file header first
    IntegrityStatus header_status = ValidateFileHeader(file, filepath);
    if (header_status != IntegrityStatus::OK) {
        return header_status;
    }
    
    // Validate file blocks
    return ValidateFileBlocks(file, filepath);
}

IntegrityStatus IntegrityChecker::ValidateSSTable(const std::string& sstable_path) {
    if (!IsValidSSTableFile(sstable_path)) {
        return IntegrityStatus::INVALID_FORMAT;
    }
    
    std::ifstream file(sstable_path, std::ios::binary);
    if (!file.is_open()) {
        return IntegrityStatus::FILE_NOT_FOUND;
    }
    
    // For SSTable files, we need to validate the structure
    // This is a simplified validation - in a real implementation,
    // we would parse the SSTable format and validate each block
    
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    if (file_size == 0) {
        return IntegrityStatus::INVALID_FORMAT;
    }
    
    // Read and validate blocks
    const size_t block_size = sizeof(Block);
    size_t blocks_read = 0;
    
    while (file.tellg() < static_cast<std::streampos>(file_size)) {
        Block block;
        file.read(reinterpret_cast<char*>(&block), sizeof(Block));
        
        if (file.gcount() != sizeof(Block)) {
            break; // End of file or partial block
        }
        
        if (!block.verify_crc32()) {
            return IntegrityStatus::CORRUPTION_DETECTED;
        }
        
        blocks_read++;
    }
    
    return IntegrityStatus::OK;
}

IntegrityStatus IntegrityChecker::ValidateWAL(const std::string& wal_path) {
    if (!IsValidWALFile(wal_path)) {
        return IntegrityStatus::INVALID_FORMAT;
    }
    
    std::ifstream file(wal_path, std::ios::binary);
    if (!file.is_open()) {
        return IntegrityStatus::FILE_NOT_FOUND;
    }
    
    // For WAL files, validate each entry
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    if (file_size == 0) {
        return IntegrityStatus::OK; // Empty WAL is valid
    }
    
    // Read and validate WAL blocks
    while (file.tellg() < static_cast<std::streampos>(file_size)) {
        WALBlock wal_block;
        file.read(reinterpret_cast<char*>(&wal_block), sizeof(WALBlock));
        
        if (file.gcount() != sizeof(WALBlock)) {
            break; // End of file or partial block
        }
        
        if (!wal_block.verify_crc32()) {
            return IntegrityStatus::CORRUPTION_DETECTED;
        }
    }
    
    return IntegrityStatus::OK;
}

IntegrityChecker::ValidationReport IntegrityChecker::ValidateMultipleFiles(const std::vector<std::string>& file_paths) {
    ValidationReport report;
    report.total_files = file_paths.size();
    
    for (const auto& filepath : file_paths) {
        IntegrityStatus status = ValidateFile(filepath);
        
        switch (status) {
            case IntegrityStatus::OK:
                report.valid_files++;
                break;
            case IntegrityStatus::CORRUPTION_DETECTED:
                report.corrupted_files++;
                report.corrupted_file_paths.push_back(filepath);
                break;
            case IntegrityStatus::FILE_NOT_FOUND:
                report.missing_files++;
                report.missing_file_paths.push_back(filepath);
                break;
            default:
                report.corrupted_files++;
                report.corrupted_file_paths.push_back(filepath);
                break;
        }
    }
    
    report.integrity_rate = report.total_files > 0 ? 
        static_cast<double>(report.valid_files) / report.total_files : 0.0;
    
    return report;
}

IntegrityStatus IntegrityChecker::PerformStartupIntegrityCheck(const std::string& db_directory) {
    std::vector<std::string> critical_files;
    
    // Collect critical files from the database directory
    try {
        for (const auto& entry : std::filesystem::directory_iterator(db_directory)) {
            if (entry.is_regular_file()) {
                std::string filepath = entry.path().string();
                std::string filename = entry.path().filename().string();
                
                // Check for SSTable files (*.sst)
                if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".sst") {
                    critical_files.push_back(filepath);
                }
                // Check for WAL files (*.wal)
                else if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".wal") {
                    critical_files.push_back(filepath);
                }
                // Check for manifest files
                else if (filename == "MANIFEST" || filename.find("MANIFEST-") == 0) {
                    critical_files.push_back(filepath);
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return IntegrityStatus::IO_ERROR;
    }
    
    if (critical_files.empty()) {
        return IntegrityStatus::OK; // No files to validate
    }
    
    ValidationReport report = ValidateMultipleFiles(critical_files);
    
    if (report.corrupted_files > 0) {
        return IntegrityStatus::CORRUPTION_DETECTED;
    }
    
    return IntegrityStatus::OK;
}

IntegrityStatus IntegrityChecker::ValidateMemTableBlock(const void* block_data, size_t block_size, uint32_t expected_crc32) {
    uint32_t calculated_crc32 = CRC32::calculate(block_data, block_size);
    
    if (calculated_crc32 == expected_crc32) {
        return IntegrityStatus::OK;
    } else {
        return IntegrityStatus::CORRUPTION_DETECTED;
    }
}

std::string IntegrityChecker::StatusToString(IntegrityStatus status) {
    switch (status) {
        case IntegrityStatus::OK:
            return "OK";
        case IntegrityStatus::CORRUPTION_DETECTED:
            return "CORRUPTION_DETECTED";
        case IntegrityStatus::FILE_NOT_FOUND:
            return "FILE_NOT_FOUND";
        case IntegrityStatus::IO_ERROR:
            return "IO_ERROR";
        case IntegrityStatus::INVALID_FORMAT:
            return "INVALID_FORMAT";
        default:
            return "UNKNOWN";
    }
}

void IntegrityChecker::PrintValidationReport(const ValidationReport& report) {
    std::cout << "=== Integrity Validation Report ===" << std::endl;
    std::cout << "Total files: " << report.total_files << std::endl;
    std::cout << "Valid files: " << report.valid_files << std::endl;
    std::cout << "Corrupted files: " << report.corrupted_files << std::endl;
    std::cout << "Missing files: " << report.missing_files << std::endl;
    std::cout << "Integrity rate: " << std::fixed << std::setprecision(2) 
              << (report.integrity_rate * 100) << "%" << std::endl;
    
    if (!report.corrupted_file_paths.empty()) {
        std::cout << "Corrupted files:" << std::endl;
        for (const auto& filepath : report.corrupted_file_paths) {
            std::cout << "  - " << filepath << std::endl;
        }
    }
    
    if (!report.missing_file_paths.empty()) {
        std::cout << "Missing files:" << std::endl;
        for (const auto& filepath : report.missing_file_paths) {
            std::cout << "  - " << filepath << std::endl;
        }
    }
    std::cout << "===================================" << std::endl;
}

// Private helper methods
bool IntegrityChecker::IsValidSSTableFile(const std::string& filepath) {
    return filepath.size() > 4 && filepath.substr(filepath.size() - 4) == ".sst";
}

bool IntegrityChecker::IsValidWALFile(const std::string& filepath) {
    return filepath.size() > 4 && filepath.substr(filepath.size() - 4) == ".wal";
}

IntegrityStatus IntegrityChecker::ValidateFileHeader(std::ifstream& file, const std::string& filepath) {
    // This is a simplified header validation
    // In a real implementation, we would validate specific header formats
    
    file.seekg(0, std::ios::beg);
    char header[16];
    file.read(header, sizeof(header));
    
    if (file.gcount() < sizeof(header)) {
        return IntegrityStatus::INVALID_FORMAT;
    }
    
    // Basic sanity check - ensure header is not all zeros
    bool all_zeros = true;
    for (size_t i = 0; i < sizeof(header); i++) {
        if (header[i] != 0) {
            all_zeros = false;
            break;
        }
    }
    
    if (all_zeros) {
        return IntegrityStatus::INVALID_FORMAT;
    }
    
    return IntegrityStatus::OK;
}

IntegrityStatus IntegrityChecker::ValidateFileBlocks(std::ifstream& file, const std::string& filepath) {
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Skip header for block validation
    file.seekg(16, std::ios::beg);
    
    const size_t block_size = sizeof(Block);
    size_t remaining_size = file_size - 16;
    
    while (remaining_size >= block_size) {
        Block block;
        file.read(reinterpret_cast<char*>(&block), sizeof(Block));
        
        if (file.gcount() != sizeof(Block)) {
            break;
        }
        
        if (!block.verify_crc32()) {
            return IntegrityStatus::CORRUPTION_DETECTED;
        }
        
        remaining_size -= block_size;
    }
    
    return IntegrityStatus::OK;
}