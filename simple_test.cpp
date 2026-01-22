#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <string>

#include "src/sstable/sstable_writer.h"
#include "src/sstable/sstable_reader.h"
#include "src/cache/block_cache.h"
#include "src/storage/versioned_value.h"

int main() {
    std::cout << "Testing basic SSTable functionality..." << std::endl;
    
    // Create simple test data
    std::map<std::string, std::vector<VersionedValue>> data;
    data["key1"] = {VersionedValue(1, "value1")};
    data["key2"] = {VersionedValue(2, "value2")};
    data["key3"] = {VersionedValue(3, "value3")};
    
    std::string filename = "simple_test.sst";
    
    // Test original format
    std::cout << "Writing with original format..." << std::endl;
    SSTableWriter::write(filename, data);
    
    // Test reading
    std::cout << "Reading back data..." << std::endl;
    BlockCache cache(100);
    
    for (const auto& [key, versions] : data) {
        auto result = SSTableReader::get(filename, key, cache);
        if (result.has_value()) {
            std::cout << "✓ " << key << " = " << result.value() << std::endl;
        } else {
            std::cout << "✗ Failed to read " << key << std::endl;
            return 1;
        }
    }
    
    // Test enhanced format
    std::cout << "Writing with enhanced format..." << std::endl;
    SSTableWriter::Config config;
    SSTableWriter::write_with_block_index(filename + "_enhanced", data, config);
    
    std::cout << "Reading back enhanced data..." << std::endl;
    BlockCache cache2(100);
    
    for (const auto& [key, versions] : data) {
        auto result = SSTableReader::get(filename + "_enhanced", key, cache2);
        if (result.has_value()) {
            std::cout << "✓ " << key << " = " << result.value() << std::endl;
        } else {
            std::cout << "✗ Failed to read " << key << " from enhanced format" << std::endl;
            return 1;
        }
    }
    
    std::cout << "All tests passed!" << std::endl;
    return 0;
}