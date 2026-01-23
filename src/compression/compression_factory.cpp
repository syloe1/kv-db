#include "compression_interface.h"
#include "snappy_compressor.h"
#include "columnar_compressor.h"
#include <iostream>

std::unique_ptr<Compressor> CompressionFactory::create_compressor(CompressionType type) {
    switch (type) {
        case CompressionType::NONE:
            return std::make_unique<NoCompressor>();
            
        case CompressionType::SNAPPY:
            return std::make_unique<SnappyCompressor>();
            
        case CompressionType::LZ4:
            return std::make_unique<LZ4Compressor>();
            
        case CompressionType::ZSTD:
            return std::make_unique<ZSTDCompressor>();
            
        case CompressionType::COLUMNAR:
            return std::make_unique<ColumnarCompressor>();
            
        default:
            std::cerr << "[CompressionFactory] 未知的压缩类型: " << static_cast<int>(type) << "\n";
            return std::make_unique<NoCompressor>();
    }
}

std::vector<CompressionType> CompressionFactory::get_available_types() {
    return {
        CompressionType::NONE,
        CompressionType::SNAPPY,
        CompressionType::LZ4,
        CompressionType::ZSTD,
        CompressionType::COLUMNAR
    };
}

std::string CompressionFactory::type_to_string(CompressionType type) {
    switch (type) {
        case CompressionType::NONE: return "None";
        case CompressionType::SNAPPY: return "Snappy";
        case CompressionType::LZ4: return "LZ4";
        case CompressionType::ZSTD: return "ZSTD";
        case CompressionType::COLUMNAR: return "Columnar";
        default: return "Unknown";
    }
}

CompressionType CompressionFactory::string_to_type(const std::string& type_str) {
    if (type_str == "None") return CompressionType::NONE;
    if (type_str == "Snappy") return CompressionType::SNAPPY;
    if (type_str == "LZ4") return CompressionType::LZ4;
    if (type_str == "ZSTD") return CompressionType::ZSTD;
    if (type_str == "Columnar") return CompressionType::COLUMNAR;
    
    std::cerr << "[CompressionFactory] 未知的压缩类型字符串: " << type_str << "\n";
    return CompressionType::NONE;
}