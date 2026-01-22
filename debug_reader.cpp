#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <string>

#include "src/sstable/sstable_writer.h"
#include "src/sstable/sstable_reader.h"
#include "src/sstable/block_index.h"
#include "src/cache/block_cache.h"
#include "src/storage/versioned_value.h"

int main() {
    std::cout << "Debug enhanced reader..." << std::endl;
    
    // Create simple test data
    std::map<std::string, std::vector<VersionedValue>> data;
    data["key1"] = {VersionedValue(1, "value1")};
    data["key2"] = {VersionedValue(2, "value2")};
    
    std::string filename = "debug.sst";
    
    // Write with enhanced format
    SSTableWriter::Config config;
    SSTableWriter::write_with_block_index(filename, data, config);
    
    std::cout << "File written. Checking format detection..." << std::endl;
    
    // Check format detection
    std::ifstream in(filename);
    if (!in.is_open()) {
        std::cout << "Cannot open file" << std::endl;
        return 1;
    }
    
    // Manual format detection
    in.seekg(0, std::ios::end);
    std::streampos file_size = in.tellg();
    std::cout << "File size: " << file_size << std::endl;
    
    // Read last few lines manually
    std::vector<std::string> lines;
    long pos = (long)file_size - 1;
    
    for (int line_count = 0; line_count < 3 && pos > 0; ++line_count) {
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
        std::cout << "Line " << line_count << ": '" << line << "'" << std::endl;
        
        pos -= line.length() + 1;
    }
    
    bool is_enhanced = lines.size() >= 2 && lines[0] == "ENHANCED_SSTABLE_V1";
    std::cout << "Is enhanced format: " << (is_enhanced ? "YES" : "NO") << std::endl;
    
    in.close();
    
    // Try reading
    BlockCache cache(100);
    auto result = SSTableReader::get(filename, "key1", cache);
    if (result.has_value()) {
        std::cout << "✓ Successfully read key1 = " << result.value() << std::endl;
    } else {
        std::cout << "✗ Failed to read key1" << std::endl;
    }
    
    return 0;
}