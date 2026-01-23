#include "crc_checksum.h"
#include <chrono>
#include <iostream>
#include <iomanip>

// Static member initialization
uint32_t CRC32::crc_table_[256];
bool CRC32::table_initialized_ = false;

void CRC32::initialize_table() {
    if (table_initialized_) return;
    
    const uint32_t polynomial = 0xEDB88320; // IEEE 802.3 polynomial
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc >>= 1;
            }
        }
        crc_table_[i] = crc;
    }
    
    table_initialized_ = true;
}

uint32_t CRC32::calculate(const void* data, size_t length) {
    initialize_table();
    
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; i++) {
        uint8_t table_index = (crc ^ bytes[i]) & 0xFF;
        crc = (crc >> 8) ^ crc_table_[table_index];
    }
    
    return crc ^ 0xFFFFFFFF;
}

uint32_t CRC32::calculate(const std::vector<uint8_t>& data) {
    return calculate(data.data(), data.size());
}

uint32_t CRC32::calculate(const std::string& data) {
    return calculate(data.data(), data.size());
}

uint32_t CRC32::update(uint32_t crc, const void* data, size_t length) {
    initialize_table();
    
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    crc ^= 0xFFFFFFFF; // Undo final XOR from previous calculation
    
    for (size_t i = 0; i < length; i++) {
        uint8_t table_index = (crc ^ bytes[i]) & 0xFF;
        crc = (crc >> 8) ^ crc_table_[table_index];
    }
    
    return crc ^ 0xFFFFFFFF;
}

bool CRC32::verify(const void* data, size_t length, uint32_t expected_crc) {
    return calculate(data, length) == expected_crc;
}

bool CRC32::verify(const std::vector<uint8_t>& data, uint32_t expected_crc) {
    return calculate(data) == expected_crc;
}

// ChecksummedBlock implementation
ChecksummedBlock::ChecksummedBlock(const std::vector<uint8_t>& block_data, uint32_t id) 
    : data(block_data), block_id(id) {
    checksum = CRC32::calculate(data);
    timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

ChecksummedBlock::ChecksummedBlock(const std::string& block_data, uint32_t id) 
    : block_id(id) {
    data.assign(block_data.begin(), block_data.end());
    checksum = CRC32::calculate(data);
    timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool ChecksummedBlock::verify_integrity() const {
    return CRC32::verify(data, checksum);
}

std::vector<uint8_t> ChecksummedBlock::serialize() const {
    std::vector<uint8_t> result;
    
    // Serialize: [block_id(4)] [timestamp(8)] [checksum(4)] [data_size(4)] [data...]
    result.resize(20 + data.size());
    
    // Block ID (4 bytes)
    *reinterpret_cast<uint32_t*>(result.data()) = block_id;
    
    // Timestamp (8 bytes)
    *reinterpret_cast<uint64_t*>(result.data() + 4) = timestamp;
    
    // Checksum (4 bytes)
    *reinterpret_cast<uint32_t*>(result.data() + 12) = checksum;
    
    // Data size (4 bytes)
    uint32_t data_size = static_cast<uint32_t>(data.size());
    *reinterpret_cast<uint32_t*>(result.data() + 16) = data_size;
    
    // Data
    std::copy(data.begin(), data.end(), result.begin() + 20);
    
    return result;
}

ChecksummedBlock ChecksummedBlock::deserialize(const std::vector<uint8_t>& serialized_data) {
    return deserialize(serialized_data.data(), serialized_data.size());
}

ChecksummedBlock ChecksummedBlock::deserialize(const uint8_t* data, size_t size) {
    if (size < 20) {
        throw std::runtime_error("Invalid serialized data: too small");
    }
    
    ChecksummedBlock block;
    
    // Read block ID
    block.block_id = *reinterpret_cast<const uint32_t*>(data);
    
    // Read timestamp
    block.timestamp = *reinterpret_cast<const uint64_t*>(data + 4);
    
    // Read checksum
    block.checksum = *reinterpret_cast<const uint32_t*>(data + 12);
    
    // Read data size
    uint32_t data_size = *reinterpret_cast<const uint32_t*>(data + 16);
    
    if (size < 20 + data_size) {
        throw std::runtime_error("Invalid serialized data: data size mismatch");
    }
    
    // Read data
    block.data.assign(data + 20, data + 20 + data_size);
    
    return block;
}

size_t ChecksummedBlock::serialized_size() const {
    return 20 + data.size();
}

// ChecksumManager implementation
ChecksummedBlock ChecksumManager::create_checksummed_block(const std::vector<uint8_t>& data, uint32_t block_id) {
    if (block_id == 0) {
        block_id = next_block_id_++;
    }
    return ChecksummedBlock(data, block_id);
}

ChecksummedBlock ChecksumManager::create_checksummed_block(const std::string& data, uint32_t block_id) {
    if (block_id == 0) {
        block_id = next_block_id_++;
    }
    return ChecksummedBlock(data, block_id);
}

ChecksumManager::ValidationResult ChecksumManager::validate_blocks(const std::vector<ChecksummedBlock>& blocks) {
    ValidationResult result;
    result.total_blocks = blocks.size();
    result.valid_blocks = 0;
    result.corrupted_blocks = 0;
    
    for (const auto& block : blocks) {
        if (block.verify_integrity()) {
            result.valid_blocks++;
        } else {
            result.corrupted_blocks++;
            result.corrupted_block_ids.push_back(block.block_id);
        }
    }
    
    result.integrity_rate = result.total_blocks > 0 ? 
        static_cast<double>(result.valid_blocks) / result.total_blocks : 0.0;
    
    return result;
}

bool ChecksumManager::repair_block(ChecksummedBlock& corrupted_block, 
                                  const std::vector<ChecksummedBlock>& backup_blocks) {
    // Simple repair: find a backup block with the same ID
    for (const auto& backup : backup_blocks) {
        if (backup.block_id == corrupted_block.block_id && backup.verify_integrity()) {
            corrupted_block = backup;
            return true;
        }
    }
    return false;
}

void ChecksumManager::print_validation_report(const ValidationResult& result) {
    std::cout << "=== Checksum Validation Report ===" << std::endl;
    std::cout << "Total blocks: " << result.total_blocks << std::endl;
    std::cout << "Valid blocks: " << result.valid_blocks << std::endl;
    std::cout << "Corrupted blocks: " << result.corrupted_blocks << std::endl;
    std::cout << "Integrity rate: " << std::fixed << std::setprecision(2) 
              << (result.integrity_rate * 100) << "%" << std::endl;
    
    if (!result.corrupted_block_ids.empty()) {
        std::cout << "Corrupted block IDs: ";
        for (size_t i = 0; i < result.corrupted_block_ids.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << result.corrupted_block_ids[i];
        }
        std::cout << std::endl;
    }
    std::cout << "=================================" << std::endl;
}