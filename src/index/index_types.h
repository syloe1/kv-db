#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>

// 索引类型枚举
enum class IndexType {
    SECONDARY,      // 二级索引
    COMPOSITE,      // 复合索引
    FULLTEXT,       // 全文索引
    INVERTED        // 倒排索引
};

// 索引元数据
struct IndexMetadata {
    std::string name;
    IndexType type;
    std::vector<std::string> fields;  // 索引字段
    bool is_unique;
    size_t memory_usage;
    size_t disk_usage;
    
    IndexMetadata(const std::string& n, IndexType t, 
                  const std::vector<std::string>& f, bool unique = false)
        : name(n), type(t), fields(f), is_unique(unique), 
          memory_usage(0), disk_usage(0) {}
};

// 索引查询结果
struct IndexLookupResult {
    std::vector<std::string> keys;
    bool success;
    std::string error_message;
    double query_time_ms;
    
    IndexLookupResult() : success(true), query_time_ms(0.0) {}
};

// 索引统计信息
struct IndexStats {
    size_t total_entries;
    size_t unique_values;
    double selectivity;  // 选择性 (unique_values / total_entries)
    size_t memory_bytes;
    size_t disk_bytes;
    
    IndexStats() : total_entries(0), unique_values(0), 
                   selectivity(0.0), memory_bytes(0), disk_bytes(0) {}
};

// 查询条件类型
enum class QueryType {
    EXACT_MATCH,    // 精确匹配
    RANGE_QUERY,    // 范围查询
    PREFIX_MATCH,   // 前缀匹配
    FULLTEXT_SEARCH,// 全文搜索
    PHRASE_SEARCH,  // 短语搜索
    BOOLEAN_SEARCH  // 布尔搜索
};

// 查询条件
struct IndexQuery {
    QueryType type;
    std::string field;
    std::string value;
    std::string range_start;
    std::string range_end;
    std::vector<std::string> terms;  // 用于复合查询
    
    IndexQuery() : type(QueryType::EXACT_MATCH) {}
    IndexQuery(QueryType t, const std::string& f, const std::string& v)
        : type(t), field(f), value(v) {}
};