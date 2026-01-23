#include "integrity_checker.h"
#include "enhanced_block.h"
#include "crc_checksum.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

void test_crc32_basic() {
    std::cout << "Testing CRC32 basic functionality..." << std::endl;
    
    std::string test_data = "Hello, World!";
    uint32_t crc1 = CRC32::calculate(test_data);
    uint32_t crc2 = CRC32::calculate(test_data.data(), test_data.size());
    
    assert(crc1 == crc2);
    assert(CRC32::verify(test_data.data(), test_data.size(), crc1));
    assert(!CRC32::verify((test_data + "x").data(), (test_data + "x").size(), crc1)); // Should fail with modified data
    
    std::cout << "CRC32 basic test passed!" << std::endl;
}

void test_integrity_checker() {
    std::cout << "Testing IntegrityChecker..." << std::endl;
    
    // Test CRC32 calculation
    std::string data = "Test data for integrity checking";
    uint32_t crc = IntegrityChecker::CalculateCRC32(data);
    assert(crc != 0);
    
    // Test block verification
    Block block;
    block.set_data(data.data(), static_cast<uint32_t>(data.size()));
    assert(IntegrityChecker::VerifyCRC32(block));
    assert(IntegrityChecker::VerifyBlock(block) == IntegrityStatus::OK);
    
    // Test corruption detection
    block.crc32 = 0; // Corrupt the checksum
    assert(!IntegrityChecker::VerifyCRC32(block));
    assert(IntegrityChecker::VerifyBlock(block) == IntegrityStatus::CORRUPTION_DETECTED);
    
    std::cout << "IntegrityChecker test passed!" << std::endl;
}

void test_enhanced_sstable_block() {
    std::cout << "Testing EnhancedSSTableBlock..." << std::endl;
    
    std::string test_data = "key1 123 value1\nkey2 124 value2\n";
    EnhancedSSTableBlock block(test_data);
    
    // Test integrity
    assert(block.verify_integrity());
    assert(block.validate() == IntegrityStatus::OK);
    
    // Test serialization/deserialization
    std::vector<uint8_t> serialized = block.serialize();
    EnhancedSSTableBlock deserialized = EnhancedSSTableBlock::deserialize(serialized);
    
    assert(deserialized.verify_integrity());
    assert(deserialized.get_data_as_string() == test_data);
    
    std::cout << "EnhancedSSTableBlock test passed!" << std::endl;
}

void test_enhanced_wal_block() {
    std::cout << "Testing EnhancedWALBlock..." << std::endl;
    
    uint64_t lsn = 12345;
    std::string entry_data = "PUT key1 value1";
    EnhancedWALBlock block(lsn, entry_data);
    
    // Test integrity
    assert(block.verify_integrity());
    assert(block.validate() == IntegrityStatus::OK);
    assert(block.get_lsn() == lsn);
    assert(block.get_entry_data_as_string() == entry_data);
    
    // Test serialization/deserialization
    std::vector<uint8_t> serialized = block.serialize();
    EnhancedWALBlock deserialized = EnhancedWALBlock::deserialize(serialized);
    
    assert(deserialized.verify_integrity());
    assert(deserialized.get_lsn() == lsn);
    assert(deserialized.get_entry_data_as_string() == entry_data);
    
    std::cout << "EnhancedWALBlock test passed!" << std::endl;
}

void test_enhanced_memtable_block() {
    std::cout << "Testing EnhancedMemTableBlock..." << std::endl;
    
    std::string entries_text = "entry1|entry2|entry3";
    std::vector<uint8_t> entries_data(entries_text.begin(), entries_text.end());
    uint32_t entry_count = 3;
    
    EnhancedMemTableBlock block(entries_data, entry_count);
    
    // Test integrity
    assert(block.verify_integrity());
    assert(block.validate() == IntegrityStatus::OK);
    assert(block.get_entry_count() == entry_count);
    
    // Test serialization/deserialization
    std::vector<uint8_t> serialized = block.serialize();
    EnhancedMemTableBlock deserialized = EnhancedMemTableBlock::deserialize(serialized);
    
    assert(deserialized.verify_integrity());
    assert(deserialized.get_entry_count() == entry_count);
    assert(deserialized.get_entries_data() == entries_data);
    
    std::cout << "EnhancedMemTableBlock test passed!" << std::endl;
}

void test_block_factory() {
    std::cout << "Testing EnhancedBlockFactory..." << std::endl;
    
    // Test SSTable block creation and validation
    EnhancedSSTableBlock sstable_block = EnhancedBlockFactory::create_sstable_block("test data");
    std::vector<uint8_t> sstable_serialized = sstable_block.serialize();
    assert(EnhancedBlockFactory::validate_block(sstable_serialized) == IntegrityStatus::OK);
    
    // Test WAL block creation and validation
    EnhancedWALBlock wal_block = EnhancedBlockFactory::create_wal_block(100, "PUT key value");
    std::vector<uint8_t> wal_serialized = wal_block.serialize();
    assert(EnhancedBlockFactory::validate_block(wal_serialized) == IntegrityStatus::OK);
    
    // Test MemTable block creation and validation
    std::vector<uint8_t> mem_data = {1, 2, 3, 4, 5};
    EnhancedMemTableBlock mem_block = EnhancedBlockFactory::create_memtable_block(mem_data, 2);
    std::vector<uint8_t> mem_serialized = mem_block.serialize();
    assert(EnhancedBlockFactory::validate_block(mem_serialized) == IntegrityStatus::OK);
    
    std::cout << "EnhancedBlockFactory test passed!" << std::endl;
}

void test_corruption_detection() {
    std::cout << "Testing corruption detection..." << std::endl;
    
    EnhancedSSTableBlock block("original data");
    std::vector<uint8_t> serialized = block.serialize();
    
    // Corrupt the data
    if (serialized.size() > sizeof(EnhancedBlockHeader) + 5) {
        serialized[sizeof(EnhancedBlockHeader) + 5] ^= 0xFF; // Flip some bits
    }
    
    // Validation should detect corruption
    assert(EnhancedBlockFactory::validate_block(serialized) == IntegrityStatus::CORRUPTION_DETECTED);
    
    std::cout << "Corruption detection test passed!" << std::endl;
}

int main() {
    std::cout << "Running CRC32 and Enhanced Block Tests..." << std::endl;
    
    try {
        test_crc32_basic();
        test_integrity_checker();
        test_enhanced_sstable_block();
        test_enhanced_wal_block();
        test_enhanced_memtable_block();
        test_block_factory();
        test_corruption_detection();
        
        std::cout << "\nAll tests passed successfully!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
}