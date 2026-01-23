#include "integrity_checker.h"
#include "enhanced_block.h"
#include "crc_checksum.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>

void test_file_validation() {
    std::cout << "Testing file validation..." << std::endl;
    
    // Create a test file with enhanced blocks
    std::string test_file = "test_sstable.sst";
    std::ofstream out(test_file, std::ios::binary);
    
    // Write multiple enhanced blocks
    for (int i = 0; i < 5; i++) {
        std::string data = "key" + std::to_string(i) + " " + std::to_string(100 + i) + " value" + std::to_string(i);
        EnhancedSSTableBlock block(data);
        std::vector<uint8_t> serialized = block.serialize();
        out.write(reinterpret_cast<const char*>(serialized.data()), serialized.size());
    }
    out.close();
    
    // Validate the file
    IntegrityStatus status = IntegrityChecker::ValidateFile(test_file);
    assert(status == IntegrityStatus::OK);
    
    // Test startup integrity check
    status = IntegrityChecker::PerformStartupIntegrityCheck(".");
    assert(status == IntegrityStatus::OK);
    
    // Clean up
    std::filesystem::remove(test_file);
    
    std::cout << "File validation test passed!" << std::endl;
}

void test_multiple_file_validation() {
    std::cout << "Testing multiple file validation..." << std::endl;
    
    std::vector<std::string> test_files = {"test1.sst", "test2.sst", "test3.wal"};
    
    // Create test files
    for (const auto& filename : test_files) {
        std::ofstream out(filename, std::ios::binary);
        
        if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".sst") {
            EnhancedSSTableBlock block("test data for " + filename);
            std::vector<uint8_t> serialized = block.serialize();
            out.write(reinterpret_cast<const char*>(serialized.data()), serialized.size());
        } else if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".wal") {
            EnhancedWALBlock block(123, "PUT key value");
            std::vector<uint8_t> serialized = block.serialize();
            out.write(reinterpret_cast<const char*>(serialized.data()), serialized.size());
        }
        out.close();
    }
    
    // Validate multiple files
    IntegrityChecker::ValidationReport report = IntegrityChecker::ValidateMultipleFiles(test_files);
    assert(report.total_files == 3);
    assert(report.valid_files == 3);
    assert(report.corrupted_files == 0);
    assert(report.integrity_rate == 1.0);
    
    IntegrityChecker::PrintValidationReport(report);
    
    // Clean up
    for (const auto& filename : test_files) {
        std::filesystem::remove(filename);
    }
    
    std::cout << "Multiple file validation test passed!" << std::endl;
}

void test_backward_compatibility() {
    std::cout << "Testing backward compatibility..." << std::endl;
    
    // Test legacy format detection and conversion
    std::string legacy_data = "key1 123 value1\nkey2 124 value2\n";
    EnhancedSSTableBlock block = EnhancedSSTableBlock::from_legacy_data(legacy_data);
    
    assert(block.verify_integrity());
    assert(block.get_data_as_string() == legacy_data);
    
    // Test WAL legacy conversion
    EnhancedWALBlock wal_block = EnhancedWALBlock::from_legacy_entry("PUT key1 value1", 100);
    assert(wal_block.verify_integrity());
    assert(wal_block.get_lsn() == 100);
    assert(wal_block.get_entry_data_as_string() == "PUT key1 value1");
    
    std::cout << "Backward compatibility test passed!" << std::endl;
}

void test_checksum_manager() {
    std::cout << "Testing ChecksumManager..." << std::endl;
    
    ChecksumManager manager;
    
    // Create checksummed blocks
    std::vector<ChecksummedBlock> blocks;
    for (int i = 0; i < 10; i++) {
        std::string data = "Block data " + std::to_string(i);
        blocks.push_back(manager.create_checksummed_block(data, i + 1));
    }
    
    // Validate all blocks
    ChecksumManager::ValidationResult result = manager.validate_blocks(blocks);
    assert(result.total_blocks == 10);
    assert(result.valid_blocks == 10);
    assert(result.corrupted_blocks == 0);
    assert(result.integrity_rate == 1.0);
    
    // Corrupt one block
    blocks[5].checksum = 0;
    result = manager.validate_blocks(blocks);
    assert(result.total_blocks == 10);
    assert(result.valid_blocks == 9);
    assert(result.corrupted_blocks == 1);
    assert(result.corrupted_block_ids.size() == 1);
    assert(result.corrupted_block_ids[0] == 6); // Block ID 6 (index 5)
    
    manager.print_validation_report(result);
    
    std::cout << "ChecksumManager test passed!" << std::endl;
}

void test_memtable_block_validation() {
    std::cout << "Testing MemTable block validation..." << std::endl;
    
    // Create test data
    std::string entries_text = "entry1|entry2|entry3|entry4";
    std::vector<uint8_t> entries_data(entries_text.begin(), entries_text.end());
    
    // Test validation
    uint32_t expected_crc = CRC32::calculate(entries_data);
    IntegrityStatus status = IntegrityChecker::ValidateMemTableBlock(
        entries_data.data(), entries_data.size(), expected_crc);
    assert(status == IntegrityStatus::OK);
    
    // Test corruption detection
    status = IntegrityChecker::ValidateMemTableBlock(
        entries_data.data(), entries_data.size(), expected_crc + 1);
    assert(status == IntegrityStatus::CORRUPTION_DETECTED);
    
    std::cout << "MemTable block validation test passed!" << std::endl;
}

int main() {
    std::cout << "Running CRC32 Infrastructure Integration Tests..." << std::endl;
    
    try {
        test_file_validation();
        test_multiple_file_validation();
        test_backward_compatibility();
        test_checksum_manager();
        test_memtable_block_validation();
        
        std::cout << "\n=== CRC32 Infrastructure Integration Tests Summary ===" << std::endl;
        std::cout << "✓ CRC32 calculation and verification" << std::endl;
        std::cout << "✓ IntegrityChecker class with block verification" << std::endl;
        std::cout << "✓ Enhanced Block structures with CRC32 checksums" << std::endl;
        std::cout << "✓ File validation utilities" << std::endl;
        std::cout << "✓ Startup integrity checks" << std::endl;
        std::cout << "✓ Multiple file validation" << std::endl;
        std::cout << "✓ Backward compatibility with existing formats" << std::endl;
        std::cout << "✓ Corruption detection and reporting" << std::endl;
        std::cout << "✓ MemTable block validation" << std::endl;
        std::cout << "\nAll integration tests passed successfully!" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Integration test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Integration test failed with unknown exception" << std::endl;
        return 1;
    }
}