#include "compaction/compactor.h"
#include "sstable/sstable_writer.h"
#include "storage/versioned_value.h"
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>

static const std::string TOMBSTONE = "__TOMBSTONE__";

static uint64_t read_index_offset(const std::string& filename) {
    std::ifstream in(filename);
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
    
    // Footer: index_offset bloom_offset
    std::istringstream iss(last_line);
    uint64_t index_offset;
    iss >> index_offset;
    
    return index_offset;
}

std::string Compactor::compact(
    const std::vector<std::string>& sstables,
    const std::string& output_dir,
    const std::string& output_filename,
    uint64_t min_snapshot_seq
) {
    // 使用多版本格式：map<key, vector<VersionedValue>>
    std::map<std::string, std::vector<VersionedValue>> merged;

    // 1. 从旧到新合并（只读 data block，格式：key seq value）
    for (const auto& file : sstables) {
        uint64_t index_offset = read_index_offset(file);
        
        std::ifstream in(file);
        std::string line;
        uint64_t bytes_read = 0;

        std::cout << "[Compaction] 读取: " << file << std::endl;

        while (std::getline(in, line) && bytes_read < index_offset) {
            bytes_read += line.size() + 1;
            
            std::istringstream iss(line);
            std::string key;
            uint64_t seq;
            std::string value;
            if (iss >> key >> seq >> value) {
                merged[key].push_back({seq, value});
            }
        }
    }

    // 2. 对每个 key 的版本进行清理和保留
    std::map<std::string, std::vector<VersionedValue>> filtered;
    for (auto& [key, versions] : merged) {
        // 按 seq DESC 排序
        std::sort(versions.begin(), versions.end(),
                  [](const VersionedValue& a, const VersionedValue& b) {
                      return a.seq > b.seq;
                  });
        
        std::vector<VersionedValue> kept;
        bool has_latest = false;
        
        for (const auto& v : versions) {
            // 保留：最新版本 或 seq >= min_snapshot_seq 的版本
            if (!has_latest) {
                // 保留最新版本（第一个）
                kept.push_back(v);
                has_latest = true;
                if (v.value != TOMBSTONE) {
                    // 如果最新版本不是 Tombstone，可以跳过后续版本（除非需要保留给 snapshot）
                    if (v.seq >= min_snapshot_seq) {
                        // 最新版本已经 >= min_snapshot_seq，不需要保留旧版本
                        break;
                    }
                }
            } else if (v.seq >= min_snapshot_seq) {
                // 保留活跃 snapshot 可能需要的版本
                kept.push_back(v);
            }
        }
        
        // 如果所有版本都是 Tombstone 且没有活跃 snapshot，可以完全删除
        bool all_tombstone = true;
        for (const auto& v : kept) {
            if (v.value != TOMBSTONE) {
                all_tombstone = false;
                break;
            }
        }
        
        if (!all_tombstone || min_snapshot_seq > 0) {
            filtered[key] = kept;
        } else {
            std::cout << "[Compaction] 清理所有 Tombstone: " << key << std::endl;
        }
    }

    // 3. 确定输出文件名
    std::string final_output;
    if (output_filename.empty()) {
        final_output = output_dir + "/sstable_compacted.dat";
    } else {
        final_output = output_dir + "/" + output_filename;
    }
    
    // 直接写入最终文件（多版本格式）
    SSTableWriter::write(final_output, filtered);
    std::cout << "[Compaction] 输出到文件: " << final_output << std::endl;

    // 4. 删除旧 SSTable
    for (const auto& file : sstables) {
        std::filesystem::remove(file);
        std::cout << "[Compaction] 删除: " << file << std::endl;
    }

    return final_output;
}