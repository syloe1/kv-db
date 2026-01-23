#!/bin/bash

# 分段WAL测试脚本
set -e

echo "=== 分段WAL功能测试 ==="

# 创建简化的分段WAL测试
cat > wal_test.cpp << 'EOF'
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstring>

// 简化的CRC32实现
class SimpleCRC32 {
public:
    static uint32_t calculate(const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xFFFFFFFF;
        
        for (size_t i = 0; i < size; i++) {
            crc ^= bytes[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ 0xEDB88320;
                } else {
                    crc >>= 1;
                }
            }
        }
        
        return ~crc;
    }
    
    static bool verify(const void* data, size_t size, uint32_t expected) {
        return calculate(data, size) == expected;
    }
};

// 简化的WAL条目
struct SimpleWALEntry {
    uint64_t lsn;
    uint32_t entry_size;
    uint32_t crc32;
    uint32_t entry_type; // 1=PUT, 2=DELETE
    std::vector<uint8_t> data;
    
    SimpleWALEntry(uint64_t l, uint32_t type, const std::string& d) 
        : lsn(l), entry_type(type) {
        data.assign(d.begin(), d.end());
        entry_size = data.size();
        crc32 = SimpleCRC32::calculate(data.data(), data.size());
    }
    
    bool verify() const {
        return SimpleCRC32::verify(data.data(), data.size(), crc32);
    }
};
EOF

# 继续创建测试代码
cat >> wal_test.cpp << 'EOF'

// 简化的分段WAL
class SimpleSegmentedWAL {
private:
    std::string directory_;
    uint64_t current_lsn_;
    std::vector<SimpleWALEntry> entries_;
    
public:
    SimpleSegmentedWAL(const std::string& dir) : directory_(dir), current_lsn_(0) {
        std::filesystem::create_directories(directory_);
    }
    
    uint64_t write_entry(uint32_t type, const std::string& key, const std::string& value) {
        current_lsn_++;
        std::string combined = key + " " + value;
        entries_.emplace_back(current_lsn_, type, combined);
        return current_lsn_;
    }
    
    void sync_to_disk() {
        std::string filename = directory_ + "/wal_segment_1.seg";
        std::ofstream file(filename, std::ios::binary);
        
        for (const auto& entry : entries_) {
            // 简化的序列化
            file.write(reinterpret_cast<const char*>(&entry.lsn), sizeof(uint64_t));
            file.write(reinterpret_cast<const char*>(&entry.entry_size), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(&entry.crc32), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(&entry.entry_type), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(entry.data.data()), entry.data.size());
        }
        file.close();
    }
    
    std::vector<SimpleWALEntry> get_all_entries() const {
        return entries_;
    }
    
    size_t get_entry_count() const {
        return entries_.size();
    }
    
    uint64_t get_current_lsn() const {
        return current_lsn_;
    }
};

int main() {
    std::cout << "运行分段WAL测试..." << std::endl;
    
    std::string test_dir = "/tmp/wal_test";
    
    // 清理测试目录
    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }
    
    try {
        SimpleSegmentedWAL wal(test_dir);
        
        // 写入测试数据
        uint64_t lsn1 = wal.write_entry(1, "key1", "value1");
        uint64_t lsn2 = wal.write_entry(1, "key2", "value2");
        uint64_t lsn3 = wal.write_entry(2, "key1", "");
        
        std::cout << "✓ 写入3个WAL条目，LSN: " << lsn1 << ", " << lsn2 << ", " << lsn3 << std::endl;
        
        // 验证条目
        auto entries = wal.get_all_entries();
        bool all_valid = true;
        for (const auto& entry : entries) {
            if (!entry.verify()) {
                all_valid = false;
                break;
            }
        }
        
        if (all_valid) {
            std::cout << "✓ 所有WAL条目CRC32验证通过" << std::endl;
        } else {
            std::cout << "✗ WAL条目CRC32验证失败" << std::endl;
            return 1;
        }
        
        // 同步到磁盘
        wal.sync_to_disk();
        std::cout << "✓ WAL数据同步到磁盘" << std::endl;
        
        // 验证文件存在
        std::string segment_file = test_dir + "/wal_segment_1.seg";
        if (std::filesystem::exists(segment_file)) {
            auto file_size = std::filesystem::file_size(segment_file);
            std::cout << "✓ WAL段文件创建成功，大小: " << file_size << " 字节" << std::endl;
        } else {
            std::cout << "✗ WAL段文件创建失败" << std::endl;
            return 1;
        }
        
        std::cout << "分段WAL测试通过!" << std::endl;
        
        // 清理
        std::filesystem::remove_all(test_dir);
        
        return 0;
    } catch (const std::exception& e) {
        std::cout << "测试失败: " << e.what() << std::endl;
        return 1;
    }
}
EOF

# 编译并运行测试
echo "编译分段WAL测试..."
if g++ -std=c++17 -o wal_test wal_test.cpp -lstdc++fs; then
    echo "✓ 编译成功"
    echo "运行测试..."
    ./wal_test
    echo "✓ 测试完成"
else
    echo "✗ 编译失败"
    exit 1
fi

# 清理
rm -f wal_test.cpp wal_test

echo "=== 分段WAL测试完成 ==="