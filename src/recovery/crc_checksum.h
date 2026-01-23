#pragma once
#include <cstdint>
#include <vector>
#include <string>

// CRC32 校验和计算器
class CRC32 {
public:
    static uint32_t calculate(const void* data, size_t length);
    static uint32_t calculate(const std::vector<uint8_t>& data);
    static uint32_t calculate(const std::string& data);
    
    // 增量计算CRC32
    static uint32_t update(uint32_t crc, const void* data, size_t length);
    
    // 验证数据完整性
    static bool verify(const void* data, size_t length, uint32_t expected_crc);
    static bool verify(const std::vector<uint8_t>& data, uint32_t expected_crc);
    
private:
    static uint32_t crc_table_[256];
    static bool table_initialized_;
    static void initialize_table();
};

// 带校验和的数据块
struct ChecksummedBlock {
    std::vector<uint8_t> data;
    uint32_t checksum;
    uint64_t timestamp;
    uint32_t block_id;
    
    ChecksummedBlock() : checksum(0), timestamp(0), block_id(0) {}
    
    ChecksummedBlock(const std::vector<uint8_t>& block_data, uint32_t id = 0);
    ChecksummedBlock(const std::string& block_data, uint32_t id = 0);
    
    // 验证数据完整性
    bool verify_integrity() const;
    
    // 序列化和反序列化
    std::vector<uint8_t> serialize() const;
    static ChecksummedBlock deserialize(const std::vector<uint8_t>& serialized_data);
    static ChecksummedBlock deserialize(const uint8_t* data, size_t size);
    
    // 获取序列化后的大小
    size_t serialized_size() const;
};

// 校验和管理器
class ChecksumManager {
public:
    ChecksumManager() = default;
    
    // 为数据块添加校验和
    ChecksummedBlock create_checksummed_block(const std::vector<uint8_t>& data, uint32_t block_id = 0);
    ChecksummedBlock create_checksummed_block(const std::string& data, uint32_t block_id = 0);
    
    // 批量验证
    struct ValidationResult {
        size_t total_blocks;
        size_t valid_blocks;
        size_t corrupted_blocks;
        std::vector<uint32_t> corrupted_block_ids;
        double integrity_rate;
    };
    
    ValidationResult validate_blocks(const std::vector<ChecksummedBlock>& blocks);
    
    // 修复损坏的块（如果有冗余数据）
    bool repair_block(ChecksummedBlock& corrupted_block, const std::vector<ChecksummedBlock>& backup_blocks);
    
    void print_validation_report(const ValidationResult& result);
    
private:
    uint32_t next_block_id_ = 1;
};