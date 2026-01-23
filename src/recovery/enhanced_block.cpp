#include "enhanced_block.h"
#include "crc_checksum.h"
#include <chrono>
#include <cstring>
#include <stdexcept>

// EnhancedBlockHeader implementation
uint32_t EnhancedBlockHeader::calculate_header_crc32() const {
    // Calculate CRC32 of header excluding the crc32 field itself
    struct HeaderForCRC {
        uint32_t magic_number;
        uint32_t version;
        uint32_t data_size;
        uint64_t timestamp;
        uint32_t block_type;
        uint32_t reserved;
    } header_for_crc;
    
    header_for_crc.magic_number = magic_number;
    header_for_crc.version = version;
    header_for_crc.data_size = data_size;
    header_for_crc.timestamp = timestamp;
    header_for_crc.block_type = block_type;
    header_for_crc.reserved = reserved;
    
    return CRC32::calculate(&header_for_crc, sizeof(header_for_crc));
}

bool EnhancedBlockHeader::verify_header_crc32() const {
    return calculate_header_crc32() == crc32;
}

// EnhancedSSTableBlock implementation
EnhancedSSTableBlock::EnhancedSSTableBlock() {
    header_.block_type = EnhancedBlockHeader::SSTABLE_BLOCK;
    header_.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

EnhancedSSTableBlock::EnhancedSSTableBlock(const std::vector<uint8_t>& data) 
    : EnhancedSSTableBlock() {
    set_data(data);
}

EnhancedSSTableBlock::EnhancedSSTableBlock(const std::string& text_data) 
    : EnhancedSSTableBlock() {
    set_data(text_data);
}

void EnhancedSSTableBlock::set_data(const std::vector<uint8_t>& data) {
    if (data.size() > MAX_BLOCK_SIZE - sizeof(EnhancedBlockHeader)) {
        throw std::runtime_error("Block data too large");
    }
    data_ = data;
    header_.data_size = static_cast<uint32_t>(data_.size());
    calculate_crc32();
}

void EnhancedSSTableBlock::set_data(const std::string& text_data) {
    std::vector<uint8_t> binary_data(text_data.begin(), text_data.end());
    set_data(binary_data);
}

std::string EnhancedSSTableBlock::get_data_as_string() const {
    return std::string(data_.begin(), data_.end());
}

void EnhancedSSTableBlock::calculate_crc32() {
    header_.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    header_.crc32 = CRC32::calculate(data_.data(), data_.size());
}

bool EnhancedSSTableBlock::verify_integrity() const {
    return header_.is_valid() && 
           CRC32::verify(data_.data(), data_.size(), header_.crc32);
}

IntegrityStatus EnhancedSSTableBlock::validate() const {
    if (!header_.is_valid()) {
        return IntegrityStatus::INVALID_FORMAT;
    }
    
    if (!verify_integrity()) {
        return IntegrityStatus::CORRUPTION_DETECTED;
    }
    
    return IntegrityStatus::OK;
}
std::vector<uint8_t> EnhancedSSTableBlock::serialize() const {
    std::vector<uint8_t> result;
    size_t total_size = sizeof(EnhancedBlockHeader) + data_.size();
    result.resize(total_size);
    
    // Copy header
    memcpy(result.data(), &header_, sizeof(EnhancedBlockHeader));
    
    // Copy data
    if (!data_.empty()) {
        memcpy(result.data() + sizeof(EnhancedBlockHeader), data_.data(), data_.size());
    }
    
    return result;
}

EnhancedSSTableBlock EnhancedSSTableBlock::deserialize(const std::vector<uint8_t>& serialized_data) {
    if (serialized_data.size() < sizeof(EnhancedBlockHeader)) {
        throw std::runtime_error("Invalid serialized data: too small for header");
    }
    
    EnhancedSSTableBlock block;
    
    // Copy header
    memcpy(&block.header_, serialized_data.data(), sizeof(EnhancedBlockHeader));
    
    // Validate header
    if (!block.header_.is_valid()) {
        throw std::runtime_error("Invalid block header");
    }
    
    // Copy data
    size_t data_size = serialized_data.size() - sizeof(EnhancedBlockHeader);
    if (data_size != block.header_.data_size) {
        throw std::runtime_error("Data size mismatch");
    }
    
    if (data_size > 0) {
        block.data_.resize(data_size);
        memcpy(block.data_.data(), 
               serialized_data.data() + sizeof(EnhancedBlockHeader), 
               data_size);
    }
    
    return block;
}

EnhancedSSTableBlock EnhancedSSTableBlock::deserialize(std::istream& in) {
    EnhancedBlockHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(EnhancedBlockHeader));
    
    if (in.gcount() != sizeof(EnhancedBlockHeader)) {
        throw std::runtime_error("Failed to read block header");
    }
    
    if (!header.is_valid()) {
        throw std::runtime_error("Invalid block header");
    }
    
    std::vector<uint8_t> data(header.data_size);
    if (header.data_size > 0) {
        in.read(reinterpret_cast<char*>(data.data()), header.data_size);
        if (in.gcount() != static_cast<std::streamsize>(header.data_size)) {
            throw std::runtime_error("Failed to read block data");
        }
    }
    
    EnhancedSSTableBlock block(data);
    block.header_ = header;
    return block;
}

size_t EnhancedSSTableBlock::get_total_size() const {
    return sizeof(EnhancedBlockHeader) + data_.size();
}

bool EnhancedSSTableBlock::is_legacy_format(const std::string& data) const {
    // Simple heuristic: legacy format is plain text
    return data.find('\n') != std::string::npos || data.find(' ') != std::string::npos;
}

EnhancedSSTableBlock EnhancedSSTableBlock::from_legacy_data(const std::string& legacy_data) {
    return EnhancedSSTableBlock(legacy_data);
}

// EnhancedWALBlock implementation
EnhancedWALBlock::EnhancedWALBlock() : lsn_(0) {
    header_.block_type = EnhancedBlockHeader::WAL_BLOCK;
    header_.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

EnhancedWALBlock::EnhancedWALBlock(uint64_t lsn, const std::vector<uint8_t>& entry_data) 
    : EnhancedWALBlock() {
    lsn_ = lsn;
    set_entry_data(entry_data);
}

EnhancedWALBlock::EnhancedWALBlock(uint64_t lsn, const std::string& entry_text) 
    : EnhancedWALBlock() {
    lsn_ = lsn;
    set_entry_data(entry_text);
}

void EnhancedWALBlock::set_entry_data(const std::vector<uint8_t>& data) {
    if (data.size() > MAX_BLOCK_SIZE - sizeof(EnhancedBlockHeader) - sizeof(uint64_t)) {
        throw std::runtime_error("WAL entry data too large");
    }
    entry_data_ = data;
    header_.data_size = static_cast<uint32_t>(entry_data_.size());
    calculate_crc32();
}

void EnhancedWALBlock::set_entry_data(const std::string& text_data) {
    std::vector<uint8_t> binary_data(text_data.begin(), text_data.end());
    set_entry_data(binary_data);
}

std::string EnhancedWALBlock::get_entry_data_as_string() const {
    return std::string(entry_data_.begin(), entry_data_.end());
}

void EnhancedWALBlock::calculate_crc32() {
    header_.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Calculate CRC32 of LSN + entry data
    std::vector<uint8_t> combined_data;
    combined_data.resize(sizeof(uint64_t) + entry_data_.size());
    
    memcpy(combined_data.data(), &lsn_, sizeof(uint64_t));
    if (!entry_data_.empty()) {
        memcpy(combined_data.data() + sizeof(uint64_t), entry_data_.data(), entry_data_.size());
    }
    
    header_.crc32 = CRC32::calculate(combined_data.data(), combined_data.size());
}

bool EnhancedWALBlock::verify_integrity() const {
    if (!header_.is_valid()) {
        return false;
    }
    
    // Verify CRC32 of LSN + entry data
    std::vector<uint8_t> combined_data;
    combined_data.resize(sizeof(uint64_t) + entry_data_.size());
    
    memcpy(combined_data.data(), &lsn_, sizeof(uint64_t));
    if (!entry_data_.empty()) {
        memcpy(combined_data.data() + sizeof(uint64_t), entry_data_.data(), entry_data_.size());
    }
    
    return CRC32::verify(combined_data.data(), combined_data.size(), header_.crc32);
}

IntegrityStatus EnhancedWALBlock::validate() const {
    if (!header_.is_valid()) {
        return IntegrityStatus::INVALID_FORMAT;
    }
    
    if (!verify_integrity()) {
        return IntegrityStatus::CORRUPTION_DETECTED;
    }
    
    return IntegrityStatus::OK;
}

std::vector<uint8_t> EnhancedWALBlock::serialize() const {
    std::vector<uint8_t> result;
    size_t total_size = sizeof(EnhancedBlockHeader) + sizeof(uint64_t) + entry_data_.size();
    result.resize(total_size);
    
    size_t offset = 0;
    
    // Copy header
    memcpy(result.data() + offset, &header_, sizeof(EnhancedBlockHeader));
    offset += sizeof(EnhancedBlockHeader);
    
    // Copy LSN
    memcpy(result.data() + offset, &lsn_, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    // Copy entry data
    if (!entry_data_.empty()) {
        memcpy(result.data() + offset, entry_data_.data(), entry_data_.size());
    }
    
    return result;
}

EnhancedWALBlock EnhancedWALBlock::deserialize(const std::vector<uint8_t>& serialized_data) {
    if (serialized_data.size() < sizeof(EnhancedBlockHeader) + sizeof(uint64_t)) {
        throw std::runtime_error("Invalid serialized WAL data: too small");
    }
    
    EnhancedWALBlock block;
    size_t offset = 0;
    
    // Copy header
    memcpy(&block.header_, serialized_data.data() + offset, sizeof(EnhancedBlockHeader));
    offset += sizeof(EnhancedBlockHeader);
    
    // Validate header
    if (!block.header_.is_valid() || block.header_.block_type != EnhancedBlockHeader::WAL_BLOCK) {
        throw std::runtime_error("Invalid WAL block header");
    }
    
    // Copy LSN
    memcpy(&block.lsn_, serialized_data.data() + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    // Copy entry data
    size_t entry_data_size = serialized_data.size() - offset;
    if (entry_data_size != block.header_.data_size) {
        throw std::runtime_error("WAL entry data size mismatch");
    }
    
    if (entry_data_size > 0) {
        block.entry_data_.resize(entry_data_size);
        memcpy(block.entry_data_.data(), serialized_data.data() + offset, entry_data_size);
    }
    
    return block;
}

EnhancedWALBlock EnhancedWALBlock::deserialize(std::istream& in) {
    EnhancedBlockHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(EnhancedBlockHeader));
    
    if (in.gcount() != sizeof(EnhancedBlockHeader)) {
        throw std::runtime_error("Failed to read WAL block header");
    }
    
    if (!header.is_valid() || header.block_type != EnhancedBlockHeader::WAL_BLOCK) {
        throw std::runtime_error("Invalid WAL block header");
    }
    
    uint64_t lsn;
    in.read(reinterpret_cast<char*>(&lsn), sizeof(uint64_t));
    if (in.gcount() != sizeof(uint64_t)) {
        throw std::runtime_error("Failed to read WAL LSN");
    }
    
    std::vector<uint8_t> entry_data(header.data_size);
    if (header.data_size > 0) {
        in.read(reinterpret_cast<char*>(entry_data.data()), header.data_size);
        if (in.gcount() != static_cast<std::streamsize>(header.data_size)) {
            throw std::runtime_error("Failed to read WAL entry data");
        }
    }
    
    EnhancedWALBlock block(lsn, entry_data);
    block.header_ = header;
    return block;
}

size_t EnhancedWALBlock::get_total_size() const {
    return sizeof(EnhancedBlockHeader) + sizeof(uint64_t) + entry_data_.size();
}

EnhancedWALBlock EnhancedWALBlock::from_legacy_entry(const std::string& legacy_entry, uint64_t lsn) {
    return EnhancedWALBlock(lsn, legacy_entry);
}
// EnhancedMemTableBlock implementation
EnhancedMemTableBlock::EnhancedMemTableBlock() : entry_count_(0) {
    header_.block_type = EnhancedBlockHeader::MEMTABLE_BLOCK;
    header_.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

EnhancedMemTableBlock::EnhancedMemTableBlock(const std::vector<uint8_t>& entries_data, uint32_t entry_count) 
    : EnhancedMemTableBlock() {
    set_entries_data(entries_data, entry_count);
}

void EnhancedMemTableBlock::set_entries_data(const std::vector<uint8_t>& data, uint32_t count) {
    if (data.size() > MAX_BLOCK_SIZE - sizeof(EnhancedBlockHeader) - sizeof(uint32_t)) {
        throw std::runtime_error("MemTable entries data too large");
    }
    entries_data_ = data;
    entry_count_ = count;
    header_.data_size = static_cast<uint32_t>(entries_data_.size());
    calculate_crc32();
}

void EnhancedMemTableBlock::calculate_crc32() {
    header_.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Calculate CRC32 of entry count + entries data
    std::vector<uint8_t> combined_data;
    combined_data.resize(sizeof(uint32_t) + entries_data_.size());
    
    memcpy(combined_data.data(), &entry_count_, sizeof(uint32_t));
    if (!entries_data_.empty()) {
        memcpy(combined_data.data() + sizeof(uint32_t), entries_data_.data(), entries_data_.size());
    }
    
    header_.crc32 = CRC32::calculate(combined_data.data(), combined_data.size());
}

bool EnhancedMemTableBlock::verify_integrity() const {
    if (!header_.is_valid()) {
        return false;
    }
    
    // Verify CRC32 of entry count + entries data
    std::vector<uint8_t> combined_data;
    combined_data.resize(sizeof(uint32_t) + entries_data_.size());
    
    memcpy(combined_data.data(), &entry_count_, sizeof(uint32_t));
    if (!entries_data_.empty()) {
        memcpy(combined_data.data() + sizeof(uint32_t), entries_data_.data(), entries_data_.size());
    }
    
    return CRC32::verify(combined_data.data(), combined_data.size(), header_.crc32);
}

IntegrityStatus EnhancedMemTableBlock::validate() const {
    if (!header_.is_valid()) {
        return IntegrityStatus::INVALID_FORMAT;
    }
    
    if (!verify_integrity()) {
        return IntegrityStatus::CORRUPTION_DETECTED;
    }
    
    return IntegrityStatus::OK;
}

std::vector<uint8_t> EnhancedMemTableBlock::serialize() const {
    std::vector<uint8_t> result;
    size_t total_size = sizeof(EnhancedBlockHeader) + sizeof(uint32_t) + entries_data_.size();
    result.resize(total_size);
    
    size_t offset = 0;
    
    // Copy header
    memcpy(result.data() + offset, &header_, sizeof(EnhancedBlockHeader));
    offset += sizeof(EnhancedBlockHeader);
    
    // Copy entry count
    memcpy(result.data() + offset, &entry_count_, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Copy entries data
    if (!entries_data_.empty()) {
        memcpy(result.data() + offset, entries_data_.data(), entries_data_.size());
    }
    
    return result;
}

EnhancedMemTableBlock EnhancedMemTableBlock::deserialize(const std::vector<uint8_t>& serialized_data) {
    if (serialized_data.size() < sizeof(EnhancedBlockHeader) + sizeof(uint32_t)) {
        throw std::runtime_error("Invalid serialized MemTable data: too small");
    }
    
    EnhancedMemTableBlock block;
    size_t offset = 0;
    
    // Copy header
    memcpy(&block.header_, serialized_data.data() + offset, sizeof(EnhancedBlockHeader));
    offset += sizeof(EnhancedBlockHeader);
    
    // Validate header
    if (!block.header_.is_valid() || block.header_.block_type != EnhancedBlockHeader::MEMTABLE_BLOCK) {
        throw std::runtime_error("Invalid MemTable block header");
    }
    
    // Copy entry count
    memcpy(&block.entry_count_, serialized_data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Copy entries data
    size_t entries_data_size = serialized_data.size() - offset;
    if (entries_data_size != block.header_.data_size) {
        throw std::runtime_error("MemTable entries data size mismatch");
    }
    
    if (entries_data_size > 0) {
        block.entries_data_.resize(entries_data_size);
        memcpy(block.entries_data_.data(), serialized_data.data() + offset, entries_data_size);
    }
    
    return block;
}

EnhancedMemTableBlock EnhancedMemTableBlock::deserialize(std::istream& in) {
    EnhancedBlockHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(EnhancedBlockHeader));
    
    if (in.gcount() != sizeof(EnhancedBlockHeader)) {
        throw std::runtime_error("Failed to read MemTable block header");
    }
    
    if (!header.is_valid() || header.block_type != EnhancedBlockHeader::MEMTABLE_BLOCK) {
        throw std::runtime_error("Invalid MemTable block header");
    }
    
    uint32_t entry_count;
    in.read(reinterpret_cast<char*>(&entry_count), sizeof(uint32_t));
    if (in.gcount() != sizeof(uint32_t)) {
        throw std::runtime_error("Failed to read MemTable entry count");
    }
    
    std::vector<uint8_t> entries_data(header.data_size);
    if (header.data_size > 0) {
        in.read(reinterpret_cast<char*>(entries_data.data()), header.data_size);
        if (in.gcount() != static_cast<std::streamsize>(header.data_size)) {
            throw std::runtime_error("Failed to read MemTable entries data");
        }
    }
    
    EnhancedMemTableBlock block(entries_data, entry_count);
    block.header_ = header;
    return block;
}

size_t EnhancedMemTableBlock::get_total_size() const {
    return sizeof(EnhancedBlockHeader) + sizeof(uint32_t) + entries_data_.size();
}

// EnhancedBlockFactory implementation
EnhancedSSTableBlock EnhancedBlockFactory::create_sstable_block(const std::string& data) {
    return EnhancedSSTableBlock(data);
}

EnhancedWALBlock EnhancedBlockFactory::create_wal_block(uint64_t lsn, const std::string& entry) {
    return EnhancedWALBlock(lsn, entry);
}

EnhancedMemTableBlock EnhancedBlockFactory::create_memtable_block(const std::vector<uint8_t>& data, uint32_t count) {
    return EnhancedMemTableBlock(data, count);
}

EnhancedBlockHeader::BlockType EnhancedBlockFactory::detect_block_type(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(EnhancedBlockHeader)) {
        return static_cast<EnhancedBlockHeader::BlockType>(0); // Invalid
    }
    
    const EnhancedBlockHeader* header = reinterpret_cast<const EnhancedBlockHeader*>(data.data());
    if (!header->is_valid()) {
        return static_cast<EnhancedBlockHeader::BlockType>(0); // Invalid
    }
    
    return static_cast<EnhancedBlockHeader::BlockType>(header->block_type);
}

EnhancedBlockHeader::BlockType EnhancedBlockFactory::detect_block_type(std::istream& in) {
    std::streampos original_pos = in.tellg();
    
    EnhancedBlockHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(EnhancedBlockHeader));
    
    // Restore stream position
    in.seekg(original_pos);
    
    if (in.gcount() != sizeof(EnhancedBlockHeader) || !header.is_valid()) {
        return static_cast<EnhancedBlockHeader::BlockType>(0); // Invalid
    }
    
    return static_cast<EnhancedBlockHeader::BlockType>(header.block_type);
}

IntegrityStatus EnhancedBlockFactory::validate_block(const std::vector<uint8_t>& data) {
    EnhancedBlockHeader::BlockType type = detect_block_type(data);
    
    try {
        switch (type) {
            case EnhancedBlockHeader::SSTABLE_BLOCK: {
                EnhancedSSTableBlock block = EnhancedSSTableBlock::deserialize(data);
                return block.validate();
            }
            case EnhancedBlockHeader::WAL_BLOCK: {
                EnhancedWALBlock block = EnhancedWALBlock::deserialize(data);
                return block.validate();
            }
            case EnhancedBlockHeader::MEMTABLE_BLOCK: {
                EnhancedMemTableBlock block = EnhancedMemTableBlock::deserialize(data);
                return block.validate();
            }
            default:
                return IntegrityStatus::INVALID_FORMAT;
        }
    } catch (const std::exception&) {
        return IntegrityStatus::CORRUPTION_DETECTED;
    }
}

IntegrityStatus EnhancedBlockFactory::validate_block(std::istream& in) {
    EnhancedBlockHeader::BlockType type = detect_block_type(in);
    
    try {
        switch (type) {
            case EnhancedBlockHeader::SSTABLE_BLOCK: {
                EnhancedSSTableBlock block = EnhancedSSTableBlock::deserialize(in);
                return block.validate();
            }
            case EnhancedBlockHeader::WAL_BLOCK: {
                EnhancedWALBlock block = EnhancedWALBlock::deserialize(in);
                return block.validate();
            }
            case EnhancedBlockHeader::MEMTABLE_BLOCK: {
                EnhancedMemTableBlock block = EnhancedMemTableBlock::deserialize(in);
                return block.validate();
            }
            default:
                return IntegrityStatus::INVALID_FORMAT;
        }
    } catch (const std::exception&) {
        return IntegrityStatus::CORRUPTION_DETECTED;
    }
}

// BlockUtils implementation
namespace BlockUtils {
    std::vector<uint8_t> text_to_binary(const std::string& text) {
        return std::vector<uint8_t>(text.begin(), text.end());
    }
    
    std::string binary_to_text(const std::vector<uint8_t>& binary) {
        return std::string(binary.begin(), binary.end());
    }
    
    size_t calculate_block_overhead() {
        return sizeof(EnhancedBlockHeader);
    }
    
    size_t calculate_max_data_size(size_t max_block_size) {
        return max_block_size - sizeof(EnhancedBlockHeader);
    }
    
    std::vector<uint8_t> compress_block_data(const std::vector<uint8_t>& data) {
        // Placeholder for future compression implementation
        return data;
    }
    
    std::vector<uint8_t> decompress_block_data(const std::vector<uint8_t>& compressed_data) {
        // Placeholder for future decompression implementation
        return compressed_data;
    }
}