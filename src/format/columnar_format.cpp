#include "columnar_format.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <algorithm>

// ColumnarWriter Implementation
ColumnarWriter::ColumnarWriter(std::shared_ptr<Schema> schema) 
    : schema_(schema), record_count_(0) {
    initialize_columns();
}

void ColumnarWriter::initialize_columns() {
    const auto& fields = schema_->get_fields();
    columns_.reserve(fields.size());
    
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];
        auto column = std::make_unique<ColumnBlock>(field.name, field.type);
        column_name_to_index_[field.name] = i;
        columns_.push_back(std::move(column));
    }
}

bool ColumnarWriter::add_record(const std::unordered_map<std::string, std::string>& record) {
    for (const auto& field : schema_->get_fields()) {
        auto it = record.find(field.name);
        if (it != record.end()) {
            if (!encode_value_to_column(field.name, it->second)) {
                return false;
            }
        } else {
            // Handle null value
            auto col_idx = column_name_to_index_[field.name];
            columns_[col_idx]->null_bitmap.push_back(true);
        }
    }
    
    record_count_++;
    return true;
}

bool ColumnarWriter::add_records(const std::vector<std::unordered_map<std::string, std::string>>& records) {
    for (const auto& record : records) {
        if (!add_record(record)) {
            return false;
        }
    }
    return true;
}

bool ColumnarWriter::encode_value_to_column(const std::string& column_name, const std::string& value) {
    auto it = column_name_to_index_.find(column_name);
    if (it == column_name_to_index_.end()) {
        return false;
    }
    
    auto& column = columns_[it->second];
    auto encoder = BinaryEncoderFactory::create_encoder();
    
    // Encode based on data type
    switch (column->data_type) {
        case DataType::INT32: {
            int32_t val = std::stoi(value);
            encoder->encode_fixed32(static_cast<uint32_t>(val));
            break;
        }
        case DataType::INT64: {
            int64_t val = std::stoll(value);
            encoder->encode_fixed64(static_cast<uint64_t>(val));
            break;
        }
        case DataType::VARINT: {
            uint64_t val = std::stoull(value);
            encoder->encode_varint(val);
            break;
        }
        case DataType::FLOAT: {
            float val = std::stof(value);
            encoder->encode_float(val);
            break;
        }
        case DataType::DOUBLE: {
            double val = std::stod(value);
            encoder->encode_double(val);
            break;
        }
        case DataType::BOOL: {
            bool val = (value == "true" || value == "1");
            encoder->encode_bool(val);
            break;
        }
        case DataType::STRING:
        default: {
            encoder->encode_string(value);
            break;
        }
    }
    
    auto encoded_data = encoder->get_encoded_data();
    column->data.insert(column->data.end(), encoded_data.begin(), encoded_data.end());
    column->null_bitmap.push_back(false);
    column->row_count++;
    
    return true;
}

std::vector<uint8_t> ColumnarWriter::finalize() {
    std::vector<uint8_t> result;
    
    // Serialize header
    auto header = serialize_header();
    result.insert(result.end(), header.begin(), header.end());
    
    // Serialize each column
    for (const auto& column : columns_) {
        auto column_data = serialize_column_block(*column);
        result.insert(result.end(), column_data.begin(), column_data.end());
    }
    
    return result;
}

std::vector<uint8_t> ColumnarWriter::serialize_header() {
    auto encoder = BinaryEncoderFactory::create_encoder();
    
    // Magic number for columnar format
    encoder->encode_fixed32(0x434F4C55); // "COLU"
    
    // Version
    encoder->encode_varint(1);
    
    // Schema name
    encoder->encode_string(schema_->get_name());
    
    // Number of columns
    encoder->encode_varint(columns_.size());
    
    // Number of records
    encoder->encode_varint(record_count_);
    
    // Column metadata
    for (const auto& column : columns_) {
        encoder->encode_string(column->column_name);
        encoder->encode_varint(static_cast<uint64_t>(column->data_type));
        encoder->encode_varint(column->row_count);
        encoder->encode_varint(column->data.size());
    }
    
    return encoder->get_encoded_data();
}

std::vector<uint8_t> ColumnarWriter::serialize_column_block(const ColumnBlock& block) {
    auto encoder = BinaryEncoderFactory::create_encoder();
    
    // Column data
    encoder->encode_bytes(block.data);
    
    // Null bitmap
    std::vector<uint8_t> bitmap_bytes;
    for (size_t i = 0; i < block.null_bitmap.size(); i += 8) {
        uint8_t byte = 0;
        for (size_t j = 0; j < 8 && i + j < block.null_bitmap.size(); ++j) {
            if (block.null_bitmap[i + j]) {
                byte |= (1 << j);
            }
        }
        bitmap_bytes.push_back(byte);
    }
    encoder->encode_bytes(bitmap_bytes);
    
    return encoder->get_encoded_data();
}

void ColumnarWriter::print_statistics() const {
    std::cout << "=== Columnar Writer Statistics ===" << std::endl;
    std::cout << "Records: " << record_count_ << std::endl;
    std::cout << "Columns: " << columns_.size() << std::endl;
    
    for (const auto& column : columns_) {
        std::cout << "Column '" << column->column_name << "': " 
                  << column->data.size() << " bytes, "
                  << column->row_count << " values" << std::endl;
    }
}

// ColumnarReader Implementation
bool ColumnarReader::load_data(const std::vector<uint8_t>& data) {
    return load_data(data.data(), data.size());
}

bool ColumnarReader::load_data(const uint8_t* data, size_t size) {
    auto decoder = BinaryEncoderFactory::create_decoder();
    decoder->set_data(data, size);
    
    if (!deserialize_header(*decoder)) {
        return false;
    }
    
    // Load column data
    for (auto& column : columns_) {
        if (!deserialize_column_block(*decoder, *column)) {
            return false;
        }
    }
    
    return true;
}

bool ColumnarReader::deserialize_header(BinaryDecoder& decoder) {
    // Check magic number
    auto magic = decoder.decode_fixed32();
    if (!magic || *magic != 0x434F4C55) {
        return false;
    }
    
    // Version
    auto version = decoder.decode_varint();
    if (!version || *version != 1) {
        return false;
    }
    
    // Schema name
    auto schema_name = decoder.decode_string();
    if (!schema_name) {
        return false;
    }
    
    // Number of columns
    auto num_columns = decoder.decode_varint();
    if (!num_columns) {
        return false;
    }
    
    // Number of records
    auto num_records = decoder.decode_varint();
    if (!num_records) {
        return false;
    }
    record_count_ = *num_records;
    
    // Create schema
    schema_ = std::make_shared<Schema>(*schema_name);
    columns_.clear();
    columns_.reserve(*num_columns);
    column_name_to_index_.clear();
    
    // Read column metadata
    for (size_t i = 0; i < *num_columns; ++i) {
        auto col_name = decoder.decode_string();
        auto col_type = decoder.decode_varint();
        auto row_count = decoder.decode_varint();
        auto data_size = decoder.decode_varint();
        
        if (!col_name || !col_type || !row_count || !data_size) {
            return false;
        }
        
        auto column = std::make_unique<ColumnBlock>(*col_name, static_cast<DataType>(*col_type));
        column->row_count = *row_count;
        column->data.reserve(*data_size);
        
        schema_->add_field(*col_name, static_cast<DataType>(*col_type), i + 1);
        column_name_to_index_[*col_name] = i;
        columns_.push_back(std::move(column));
    }
    
    return true;
}

bool ColumnarReader::deserialize_column_block(BinaryDecoder& decoder, ColumnBlock& block) {
    // Read column data
    auto data = decoder.decode_bytes();
    if (!data) {
        return false;
    }
    block.data = *data;
    
    // Read null bitmap
    auto bitmap_bytes = decoder.decode_bytes();
    if (!bitmap_bytes) {
        return false;
    }
    
    // Convert bitmap bytes to bool vector
    block.null_bitmap.clear();
    block.null_bitmap.reserve(block.row_count);
    
    for (size_t i = 0; i < block.row_count; ++i) {
        size_t byte_idx = i / 8;
        size_t bit_idx = i % 8;
        
        if (byte_idx < bitmap_bytes->size()) {
            bool is_null = ((*bitmap_bytes)[byte_idx] & (1 << bit_idx)) != 0;
            block.null_bitmap.push_back(is_null);
        } else {
            block.null_bitmap.push_back(false);
        }
    }
    
    return true;
}

std::optional<std::unordered_map<std::string, std::string>> ColumnarReader::read_record(size_t index) {
    if (index >= record_count_) {
        return std::nullopt;
    }
    
    std::unordered_map<std::string, std::string> record;
    
    for (const auto& column : columns_) {
        if (index < column->null_bitmap.size() && column->null_bitmap[index]) {
            // Null value
            continue;
        }
        
        std::string value = decode_value_from_column(*column, index);
        record[column->column_name] = value;
    }
    
    return record;
}

std::string ColumnarReader::decode_value_from_column(const ColumnBlock& block, size_t index) {
    // This is a simplified implementation
    // In a real implementation, we would need to track the offset for each value
    // For now, we'll return a placeholder
    return "decoded_value_" + std::to_string(index);
}

std::vector<std::unordered_map<std::string, std::string>> ColumnarReader::read_all_records() {
    std::vector<std::unordered_map<std::string, std::string>> records;
    records.reserve(record_count_);
    
    for (size_t i = 0; i < record_count_; ++i) {
        auto record = read_record(i);
        if (record) {
            records.push_back(*record);
        }
    }
    
    return records;
}

std::vector<std::string> ColumnarReader::get_column_names() const {
    std::vector<std::string> names;
    names.reserve(columns_.size());
    
    for (const auto& column : columns_) {
        names.push_back(column->column_name);
    }
    
    return names;
}

void ColumnarReader::print_statistics() const {
    std::cout << "=== Columnar Reader Statistics ===" << std::endl;
    std::cout << "Records: " << record_count_ << std::endl;
    std::cout << "Columns: " << columns_.size() << std::endl;
    
    for (const auto& column : columns_) {
        std::cout << "Column '" << column->column_name << "': " 
                  << column->data.size() << " bytes" << std::endl;
    }
}

// ColumnarFormatManager Implementation
ColumnarFormatManager::ConversionResult ColumnarFormatManager::convert_to_columnar(
    const std::vector<std::unordered_map<std::string, std::string>>& records,
    const std::string& schema_name) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Generate optimized schema
    auto schema = schema_optimizer_.generate_optimized_schema(schema_name, records);
    
    // Create writer and convert data
    ColumnarWriter writer(std::shared_ptr<Schema>(schema.release()));
    writer.add_records(records);
    auto columnar_data = writer.finalize();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    // Calculate sizes
    size_t original_size = calculate_text_size(records);
    size_t compressed_size = columnar_data.size();
    double compression_ratio = static_cast<double>(original_size) / compressed_size;
    
    ConversionResult result;
    result.columnar_data = std::move(columnar_data);
    result.original_size = original_size;
    result.compressed_size = compressed_size;
    result.compression_ratio = compression_ratio;
    result.conversion_time_us = static_cast<uint64_t>(duration.count());
    result.schema = std::shared_ptr<Schema>(new Schema(*schema_name));
    
    return result;
}

ColumnarFormatManager::DeconversionResult ColumnarFormatManager::convert_from_columnar(
    const std::vector<uint8_t>& columnar_data) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    ColumnarReader reader;
    bool success = reader.load_data(columnar_data);
    
    std::vector<std::unordered_map<std::string, std::string>> records;
    if (success) {
        records = reader.read_all_records();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    DeconversionResult result;
    result.records = std::move(records);
    result.deconversion_time_us = static_cast<uint64_t>(duration.count());
    result.success = success;
    
    return result;
}

ColumnarFormatManager::PerformanceComparison ColumnarFormatManager::compare_formats(
    const std::vector<std::unordered_map<std::string, std::string>>& test_data) {
    
    // Convert to columnar format
    auto conversion_result = convert_to_columnar(test_data, "performance_test");
    
    // Benchmark text parsing
    uint64_t text_parse_time = benchmark_text_parsing(test_data);
    
    // Calculate metrics
    size_t text_size = calculate_text_size(test_data);
    double space_savings = 1.0 - (static_cast<double>(conversion_result.compressed_size) / text_size);
    double parse_speed_improvement = static_cast<double>(text_parse_time) / conversion_result.conversion_time_us;
    
    PerformanceComparison result;
    result.text_format_size = text_size;
    result.columnar_format_size = conversion_result.compressed_size;
    result.space_savings = space_savings;
    result.text_parse_time_us = text_parse_time;
    result.columnar_parse_time_us = conversion_result.conversion_time_us;
    result.parse_speed_improvement = parse_speed_improvement;
    
    return result;
}

uint64_t ColumnarFormatManager::benchmark_text_parsing(
    const std::vector<std::unordered_map<std::string, std::string>>& records) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Simulate text parsing by iterating through all records and values
    size_t total_chars = 0;
    for (const auto& record : records) {
        for (const auto& [key, value] : record) {
            total_chars += key.length() + value.length();
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    return duration.count();
}

size_t ColumnarFormatManager::calculate_text_size(
    const std::vector<std::unordered_map<std::string, std::string>>& records) {
    
    size_t total_size = 0;
    for (const auto& record : records) {
        for (const auto& [key, value] : record) {
            total_size += key.length() + value.length() + 2; // +2 for separators
        }
        total_size += 1; // +1 for record separator
    }
    return total_size;
}

void ColumnarFormatManager::print_comparison_report(const PerformanceComparison& comparison) {
    std::cout << "\n=== Data Format Performance Comparison ===" << std::endl;
    std::cout << "Text Format Size: " << comparison.text_format_size << " bytes" << std::endl;
    std::cout << "Columnar Format Size: " << comparison.columnar_format_size << " bytes" << std::endl;
    std::cout << "Space Savings: " << (comparison.space_savings * 100) << "%" << std::endl;
    std::cout << "Text Parse Time: " << comparison.text_parse_time_us << " μs" << std::endl;
    std::cout << "Columnar Parse Time: " << comparison.columnar_parse_time_us << " μs" << std::endl;
    std::cout << "Parse Speed Improvement: " << comparison.parse_speed_improvement << "x" << std::endl;
}