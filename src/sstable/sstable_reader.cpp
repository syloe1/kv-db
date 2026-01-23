#include "sstable/sstable_reader.h"
#include "sstable/block_index.h"
#include "bloom/bloom_filter.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <climits>
#include <cstdint>

struct SSTableFooter {
    uint64_t index_offset;
    uint64_t bloom_offset;
};

struct EnhancedSSTableFooter {
    uint64_t data_start_offset;
    uint64_t block_index_offset;
    uint64_t bloom_offset;
};

static SSTableFooter read_footer(std::ifstream& in) {
    in.seekg(0, std::ios::end);
    std::streampos file_size = in.tellg();

    std::string last_line;
    long pos = (long)file_size - 1;

    in.seekg(pos);
    char c;
    while (pos > 0 && in.get(c) && c == '\n') {
        pos--;
        in.seekg(pos);
    }

    while (pos > 0) {
        in.seekg(pos);
        in.get(c);
        if (c == '\n') {
            pos++;
            break;
        }
        pos--;
    }
    
    in.clear();
    in.seekg(pos);
    std::getline(in, last_line);

    SSTableFooter footer = {0, 0};
    std::istringstream iss(last_line);
    iss >> footer.index_offset >> footer.bloom_offset;

    return footer;
}

static EnhancedSSTableFooter read_enhanced_footer(std::ifstream& in) {
    in.seekg(0, std::ios::end);
    std::streampos file_size = in.tellg();

    // Read last two lines
    std::vector<std::string> lines;
    long pos = (long)file_size - 1;
    
    for (int line_count = 0; line_count < 2 && pos > 0; ++line_count) {
        // Skip trailing newlines
        in.seekg(pos);
        char c;
        while (pos > 0 && in.get(c) && c == '\n') {
            pos--;
            in.seekg(pos);
        }
        
        // Find start of line
        while (pos > 0) {
            in.seekg(pos);
            in.get(c);
            if (c == '\n') {
                pos++;
                break;
            }
            pos--;
        }
        
        // Read the line
        in.clear();
        in.seekg(pos);
        std::string line;
        std::getline(in, line);
        lines.insert(lines.begin(), line);
        
        pos -= line.length() + 1;
    }
    
    EnhancedSSTableFooter footer = {0, 0, 0};
    if (lines.size() >= 2) {
        std::istringstream iss(lines[1]);
        iss >> footer.data_start_offset >> footer.block_index_offset >> footer.bloom_offset;
    }
    
    return footer;
}

bool SSTableReader::is_enhanced_format(const std::string& filename) {
    std::ifstream in(filename);
    if (!in.is_open()) {
        return false;
    }
    
    // Check for enhanced format marker in footer
    in.seekg(0, std::ios::end);
    std::streampos file_size = in.tellg();
    
    // Read last few lines
    std::vector<std::string> lines;
    long pos = (long)file_size - 1;
    
    for (int line_count = 0; line_count < 2 && pos > 0; ++line_count) {
        // Skip trailing newlines
        in.seekg(pos);
        char c;
        while (pos > 0 && in.get(c) && c == '\n') {
            pos--;
            in.seekg(pos);
        }
        
        // Find start of line
        while (pos > 0) {
            in.seekg(pos);
            in.get(c);
            if (c == '\n') {
                pos++;
                break;
            }
            pos--;
        }
        
        // Read the line
        in.clear();
        in.seekg(pos);
        std::string line;
        std::getline(in, line);
        lines.insert(lines.begin(), line);
        
        pos -= line.length() + 1;
    }
    
    // Check if second-to-last line is the enhanced marker
    return lines.size() >= 2 && lines[1] == "ENHANCED_SSTABLE_V1";
}

std::optional<std::string>
SSTableReader::get(const std::string& filename, const std::string& key, BlockCache& cache) {
    // 使用最大 uint64_t 作为 snapshot_seq（读取最新版本）
    return get(filename, key, UINT64_MAX, cache);
}

std::optional<std::string>
SSTableReader::get(const std::string& filename, const std::string& key, uint64_t snapshot_seq, BlockCache& cache) {
    // Check if this is an enhanced format file
    if (is_enhanced_format(filename)) {
        return get_with_block_index(filename, key, snapshot_seq, cache);
    }
    
    // Fall back to original implementation
    std::string cache_key = filename + ":" + key + ":" + std::to_string(snapshot_seq);

    // 1. 查 Block Cache
    auto cached = cache.get(cache_key);
    if (cached.has_value()) {
        return cached;
    }

    std::ifstream in(filename);
    if (!in.is_open()) {
        return std::nullopt;
    }

    // 2. 读取 footer
    SSTableFooter footer = read_footer(in);

    // 3. 读取 Bloom Filter
    in.clear();
    in.seekg(footer.bloom_offset);
    BloomFilter bloom(8192, 3);
    bloom.deserialize(in);
    
    if (!bloom.possiblyContains(key)) {
        return std::nullopt;
    }

    // 4. 读取 index block
    in.clear();
    in.seekg(footer.index_offset);
    
    std::vector<std::pair<std::string, uint64_t>> index;
    std::string line;
    
    // 读取 index 条目
    while (std::getline(in, line)) {
        if (!line.empty() && (line[0] == '0' || line[0] == '1') && line.size() > 100) {
            break; 
        }
        
        std::istringstream iss(line);
        std::string k;
        uint64_t off;
        if (!(iss >> k >> off)) {
            break; 
        }
        index.emplace_back(k, off);
    }

    if (index.empty()) {
        return std::nullopt;
    }

    // 5. 二分查找 key
    int l = 0, r = (int)index.size() - 1;
    int key_pos = -1;
    while (l <= r) {
        int mid = (l + r) / 2;
        if (index[mid].first == key) {
            key_pos = mid;
            break;
        }
        if (index[mid].first < key) {
            l = mid + 1;
        } else {
            r = mid - 1;
        }
    }

    if (key_pos == -1) {
        return std::nullopt;
    }

    // 6. 读取该 key 的所有版本，找到 <= snapshot_seq 的最新版本
    in.clear();
    in.seekg(index[key_pos].second);
    
    std::string result_value;
    bool found = false;
    
    // 读取该 key 的所有版本（按 seq DESC 排序）
    while (std::getline(in, line)) {
        std::istringstream data(line);
        std::string k;
        uint64_t seq;
        std::string v;
        
        if (!(data >> k >> seq >> v)) {
            break; // 读到下一个 key 或文件结束
        }
        
        if (k != key) {
            break; // 读到下一个 key
        }
        
        // 找到 <= snapshot_seq 的第一个版本（因为按 DESC 排序）
        if (seq <= snapshot_seq) {
            if (v == "__TOMBSTONE__") {
                return std::nullopt; // 被删除
            }
            result_value = v;
            found = true;
            break;
        }
    }

    if (!found) {
        return std::nullopt;
    }
    
    // 7. 写入 Cache
    cache.put(cache_key, result_value);
    return result_value;
}

std::optional<std::string>
SSTableReader::get_with_block_index(const std::string& filename, const std::string& key, 
                                   uint64_t snapshot_seq, BlockCache& cache) {
    std::string cache_key = filename + ":" + key + ":" + std::to_string(snapshot_seq);

    // 1. 查 Block Cache
    auto cached = cache.get(cache_key);
    if (cached.has_value()) {
        return cached;
    }

    std::ifstream in(filename);
    if (!in.is_open()) {
        return std::nullopt;
    }

    // 2. 读取 enhanced footer
    EnhancedSSTableFooter footer = read_enhanced_footer(in);

    // 3. 读取 Bloom Filter
    in.clear();
    in.seekg(footer.bloom_offset);
    BloomFilter bloom(8192, 3);
    bloom.deserialize(in);
    
    if (!bloom.possiblyContains(key)) {
        return std::nullopt;
    }

    // 4. 读取 block index
    in.clear();
    in.seekg(footer.block_index_offset);
    BlockIndex block_index;
    block_index.deserialize(in);

    // 5. 找到包含 key 的 block
    int block_id = block_index.find_block(key);
    if (block_id == -1) {
        return std::nullopt;
    }

    const BlockIndexEntry* block_entry = block_index.get_block(block_id);
    if (!block_entry) {
        return std::nullopt;
    }

    // 6. 从 block 中读取数据
    auto result = read_from_block(in, *block_entry, key, snapshot_seq);
    
    if (result.has_value()) {
        // 7. 写入 Cache
        cache.put(cache_key, result.value());
    }
    
    return result;
}

std::optional<std::string>
SSTableReader::read_from_block(std::ifstream& in, const BlockIndexEntry& block_entry,
                              const std::string& key, uint64_t snapshot_seq) {
    // Seek to block start
    in.clear();
    in.seekg(block_entry.offset);
    
    std::string line;
    uint64_t bytes_read = 0;
    
    // Read through the block looking for the key
    while (bytes_read < block_entry.size && std::getline(in, line)) {
        bytes_read += line.length() + 1; // +1 for newline
        
        // Check if this line contains delta-encoded data
        if (line.find(' ') != std::string::npos) {
            std::istringstream iss(line);
            std::string k;
            iss >> k;
            
            if (k == key) {
                // Parse the line for versions
                auto versions = parse_delta_encoded_versions(line);
                
                // Find the appropriate version for snapshot_seq
                for (const auto& [seq, value] : versions) {
                    if (seq <= snapshot_seq) {
                        if (value == "__TOMBSTONE__") {
                            return std::nullopt;
                        }
                        return value;
                    }
                }
                return std::nullopt; // No visible version found
            }
        }
    }
    
    return std::nullopt;
}

std::vector<std::pair<uint64_t, std::string>>
SSTableReader::parse_delta_encoded_versions(const std::string& line) {
    std::vector<std::pair<uint64_t, std::string>> versions;
    std::istringstream iss(line);
    
    std::string key;
    uint64_t base_seq;
    std::string first_value;
    
    if (!(iss >> key >> base_seq >> first_value)) {
        return versions;
    }
    
    versions.emplace_back(base_seq, first_value);
    
    // Parse delta-encoded versions
    uint32_t delta;
    std::string value;
    while (iss >> delta >> value) {
        versions.emplace_back(base_seq + delta, value);
    }
    
    return versions;
}
