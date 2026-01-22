#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <chrono>
#include <random>
#include <string>
#include <cassert>

#include "src/sstable/sstable_writer.h"
#include "src/sstable/sstable_reader.h"
#include "src/sstable/block_index.h"
#include "src/cache/block_cache.h"
#include "src/storage/versioned_value.h"

class IndexOptimizationTest {
private:
    std::string test_file_original = "test_original.sst";
    std::string test_file_optimized = "test_optimized.sst";
    
public:
    void run_all_tests() {
        std::cout << "=== SSTable Index Optimization Tests ===" << std::endl;
        
        test_block_index_basic();
        test_prefix_compression();
        test_delta_encoding();
        test_performance_comparison();
        test_backward_compatibility();
        
        std::cout << "All tests passed!" << std::endl;
    }
    
private:
    void test_block_index_basic() {
        std::cout << "Testing basic block index functionality..." << std::endl;
        
        BlockIndex index;
        
        // Add some blocks
        index.add_block("key001", "key100", 0, 1024, 50);
        index.add_block("key101", "key200", 1024, 1024, 50);
        index.add_block("key201", "key300", 2048, 1024, 50);
        
        // Test finding blocks
        assert(index.find_block("key050") == 0);
        assert(index.find_block("key150") == 1);
        assert(index.find_block("key250") == 2);
        assert(index.find_block("key000") == -1); // Before first block
        
        std::cout << "✓ Block index basic functionality works" << std::endl;
    }
    
    void test_prefix_compression() {
        std::cout << "Testing prefix compression..." << std::endl;
        
        // Create test data with common prefixes
        std::map<std::string, std::vector<VersionedValue>> data;
        for (int i = 0; i < 100; ++i) {
            std::string key = "user_profile_" + std::to_string(i);
            std::vector<VersionedValue> versions;
            versions.emplace_back(i, "value_" + std::to_string(i));
            data[key] = versions;
        }
        
        // Write with optimization
        SSTableWriter::Config config;
        config.enable_prefix_compression = true;
        SSTableWriter::write_with_block_index(test_file_optimized, data, config);
        
        // Verify we can read back the data
        BlockCache cache(1000);
        for (const auto& [key, versions] : data) {
            auto result = SSTableReader::get(test_file_optimized, key, cache);
            assert(result.has_value());
            assert(result.value() == versions[0].value);
        }
        
        std::cout << "✓ Prefix compression works correctly" << std::endl;
    }
    
    void test_delta_encoding() {
        std::cout << "Testing delta encoding..." << std::endl;
        
        // Create test data with multiple versions
        std::map<std::string, std::vector<VersionedValue>> data;
        std::string key = "multi_version_key";
        std::vector<VersionedValue> versions;
        
        // Add versions with sequence numbers that can benefit from delta encoding
        versions.emplace_back(1000, "value_v1");
        versions.emplace_back(1001, "value_v2");
        versions.emplace_back(1002, "value_v3");
        versions.emplace_back(1005, "value_v4");
        
        data[key] = versions;
        
        // Write with delta encoding
        SSTableWriter::Config config;
        config.enable_delta_encoding = true;
        SSTableWriter::write_with_block_index(test_file_optimized, data, config);
        
        // Test reading different snapshots
        BlockCache cache(1000);
        
        // Should get latest version
        auto result = SSTableReader::get(test_file_optimized, key, 2000, cache);
        assert(result.has_value());
        assert(result.value() == "value_v4");
        
        // Should get version at specific snapshot
        result = SSTableReader::get(test_file_optimized, key, 1001, cache);
        assert(result.has_value());
        assert(result.value() == "value_v2");
        
        std::cout << "✓ Delta encoding works correctly" << std::endl;
    }
    
    void test_performance_comparison() {
        std::cout << "Testing performance comparison..." << std::endl;
        
        // Generate larger dataset for performance testing
        std::map<std::string, std::vector<VersionedValue>> data;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 1000000);
        
        for (int i = 0; i < 1000; ++i) {
            std::string key = "performance_test_key_" + std::to_string(i);
            std::vector<VersionedValue> versions;
            versions.emplace_back(dis(gen), "value_" + std::to_string(i));
            data[key] = versions;
        }
        
        // Write original format
        auto start = std::chrono::high_resolution_clock::now();
        SSTableWriter::write(test_file_original, data);
        auto end = std::chrono::high_resolution_clock::now();
        auto original_write_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Write optimized format
        start = std::chrono::high_resolution_clock::now();
        SSTableWriter::Config config;
        config.block_size = 4096;
        config.sparse_index_interval = 16;
        SSTableWriter::write_with_block_index(test_file_optimized, data, config);
        end = std::chrono::high_resolution_clock::now();
        auto optimized_write_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Test read performance
        BlockCache cache(1000);
        std::vector<std::string> test_keys;
        for (int i = 0; i < 100; ++i) {
            test_keys.push_back("performance_test_key_" + std::to_string(i));
        }
        
        // Read from original format
        start = std::chrono::high_resolution_clock::now();
        for (const auto& key : test_keys) {
            auto result = SSTableReader::get(test_file_original, key, cache);
            assert(result.has_value());
        }
        end = std::chrono::high_resolution_clock::now();
        auto original_read_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Clear cache for fair comparison
        cache = BlockCache(1000);
        
        // Read from optimized format
        start = std::chrono::high_resolution_clock::now();
        for (const auto& key : test_keys) {
            auto result = SSTableReader::get(test_file_optimized, key, cache);
            assert(result.has_value());
        }
        end = std::chrono::high_resolution_clock::now();
        auto optimized_read_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << "Performance Results:" << std::endl;
        std::cout << "  Original write time: " << original_write_time.count() << " μs" << std::endl;
        std::cout << "  Optimized write time: " << optimized_write_time.count() << " μs" << std::endl;
        std::cout << "  Original read time: " << original_read_time.count() << " μs" << std::endl;
        std::cout << "  Optimized read time: " << optimized_read_time.count() << " μs" << std::endl;
        
        // Check file sizes
        std::ifstream original_file(test_file_original, std::ios::ate | std::ios::binary);
        std::ifstream optimized_file(test_file_optimized, std::ios::ate | std::ios::binary);
        
        if (original_file.is_open() && optimized_file.is_open()) {
            auto original_size = original_file.tellg();
            auto optimized_size = optimized_file.tellg();
            
            std::cout << "  Original file size: " << original_size << " bytes" << std::endl;
            std::cout << "  Optimized file size: " << optimized_size << " bytes" << std::endl;
            
            if (optimized_size < original_size) {
                double reduction = (1.0 - (double)optimized_size / original_size) * 100;
                std::cout << "  Size reduction: " << reduction << "%" << std::endl;
            }
        }
        
        std::cout << "✓ Performance comparison completed" << std::endl;
    }
    
    void test_backward_compatibility() {
        std::cout << "Testing backward compatibility..." << std::endl;
        
        // Create test data
        std::map<std::string, std::vector<VersionedValue>> data;
        for (int i = 0; i < 10; ++i) {
            std::string key = "compat_key_" + std::to_string(i);
            std::vector<VersionedValue> versions;
            versions.emplace_back(i, "compat_value_" + std::to_string(i));
            data[key] = versions;
        }
        
        // Write in original format
        SSTableWriter::write(test_file_original, data);
        
        // Should be able to read with new reader
        BlockCache cache(1000);
        for (const auto& [key, versions] : data) {
            auto result = SSTableReader::get(test_file_original, key, cache);
            assert(result.has_value());
            assert(result.value() == versions[0].value);
        }
        
        std::cout << "✓ Backward compatibility maintained" << std::endl;
    }
};

int main() {
    try {
        IndexOptimizationTest test;
        test.run_all_tests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}