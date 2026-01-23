#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <fstream>
#include <cstring>
#include "crc_checksum.h"

// Forward declarations
class SSTableReader;

// Status enum for integrity operations
enum class IntegrityStatus {
    OK,
    CORRUPTION_DETECTED,
    FILE_NOT_FOUND,
    IO_ERROR,
    INVALID_FORMAT
};

// Block structure with CRC32 checksum
struct Block {
    uint32_t crc32;        // CRC32 checksum
    uint32_t size;         // Data size
    uint64_t timestamp;    // Block timestamp
    char data[4096 - 16];  // Actual data (4KB block - 16 bytes header)
    
    Block() : crc32(0), size(0), timestamp(0) {
        memset(data, 0, sizeof(data));
    }
    
    // Calculate and set CRC32 for the data
    void calculate_crc32();
    
    // Verify the CRC32 checksum
    bool verify_crc32() const;
    
    // Get the actual data size
    uint32_t get_data_size() const { return size; }
    
    // Set data and update size and CRC32
    void set_data(const void* src, uint32_t data_size);
    
    // Get data as string
    std::string get_data_as_string() const;
};

// Integrity checker for all storage components
class IntegrityChecker {
public:
    // CRC32 calculation methods
    static uint32_t CalculateCRC32(const void* data, size_t size);
    static uint32_t CalculateCRC32(const std::string& data);
    static uint32_t CalculateCRC32(const std::vector<uint8_t>& data);
    
    // Block verification methods
    static bool VerifyCRC32(const Block& block);
    static IntegrityStatus VerifyBlock(const Block& block);
    
    // File validation utilities
    static IntegrityStatus ValidateFile(const std::string& filepath);
    static IntegrityStatus ValidateSSTable(const std::string& sstable_path);
    static IntegrityStatus ValidateWAL(const std::string& wal_path);
    
    // Batch validation for multiple files
    struct ValidationReport {
        size_t total_files;
        size_t valid_files;
        size_t corrupted_files;
        size_t missing_files;
        std::vector<std::string> corrupted_file_paths;
        std::vector<std::string> missing_file_paths;
        double integrity_rate;
        
        ValidationReport() : total_files(0), valid_files(0), corrupted_files(0), 
                           missing_files(0), integrity_rate(0.0) {}
    };
    
    static ValidationReport ValidateMultipleFiles(const std::vector<std::string>& file_paths);
    
    // Startup integrity checks
    static IntegrityStatus PerformStartupIntegrityCheck(const std::string& db_directory);
    
    // Memory table block verification
    static IntegrityStatus ValidateMemTableBlock(const void* block_data, size_t block_size, uint32_t expected_crc32);
    
    // Utility methods
    static std::string StatusToString(IntegrityStatus status);
    static void PrintValidationReport(const ValidationReport& report);
    
private:
    // Internal helper methods
    static bool IsValidSSTableFile(const std::string& filepath);
    static bool IsValidWALFile(const std::string& filepath);
    static IntegrityStatus ValidateFileHeader(std::ifstream& file, const std::string& filepath);
    static IntegrityStatus ValidateFileBlocks(std::ifstream& file, const std::string& filepath);
};

// Enhanced block structure for different storage types
struct MemTableBlock {
    uint32_t crc32;        // CRC32 checksum
    uint32_t entry_count;  // Number of entries in this block
    uint32_t block_size;   // Total block size
    uint64_t timestamp;    // Block creation timestamp
    char entries[4096 - 20]; // Entry data (4KB - 20 bytes header)
    
    MemTableBlock() : crc32(0), entry_count(0), block_size(0), timestamp(0) {
        memset(entries, 0, sizeof(entries));
    }
    
    void calculate_crc32();
    bool verify_crc32() const;
    void set_entries(const void* entry_data, uint32_t data_size, uint32_t count);
};

struct WALBlock {
    uint32_t crc32;        // CRC32 checksum
    uint64_t lsn;          // Log sequence number
    uint32_t entry_size;   // Size of the WAL entry
    uint64_t timestamp;    // Entry timestamp
    char entry_data[4096 - 24]; // WAL entry data (4KB - 24 bytes header)
    
    WALBlock() : crc32(0), lsn(0), entry_size(0), timestamp(0) {
        memset(entry_data, 0, sizeof(entry_data));
    }
    
    void calculate_crc32();
    bool verify_crc32() const;
    void set_entry_data(const void* data, uint32_t size, uint64_t sequence_number);
};