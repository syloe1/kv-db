#pragma once
#include "db/kv_db.h"
#include "iterator/iterator.h"
#include "snapshot/snapshot.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <regex>

// 查询结果结构
struct QueryResult {
    std::vector<std::pair<std::string, std::string>> results;
    size_t total_count = 0;
    bool success = true;
    std::string error_message;
};

// 聚合结果结构
struct AggregateResult {
    size_t count = 0;
    double sum = 0.0;
    double avg = 0.0;
    double min = 0.0;
    double max = 0.0;
    bool success = true;
    std::string error_message;
};

// 排序方向
enum class SortOrder {
    ASC,
    DESC
};

// 条件操作符
enum class ConditionOperator {
    EQUALS,
    NOT_EQUALS,
    LIKE,
    NOT_LIKE,
    GREATER_THAN,
    LESS_THAN,
    GREATER_EQUAL,
    LESS_EQUAL
};

// 查询条件
struct QueryCondition {
    std::string field;  // "key" 或 "value"
    ConditionOperator op;
    std::string value;
    
    QueryCondition(const std::string& f, ConditionOperator o, const std::string& v)
        : field(f), op(o), value(v) {}
};

// 高级查询引擎
class QueryEngine {
public:
    explicit QueryEngine(KVDB& db);
    
    // 批量操作
    bool batch_put(const std::vector<std::pair<std::string, std::string>>& pairs);
    QueryResult batch_get(const std::vector<std::string>& keys);
    bool batch_delete(const std::vector<std::string>& keys);
    
    // 条件查询
    QueryResult query_where(const QueryCondition& condition, size_t limit = 0);
    QueryResult query_where_multiple(const std::vector<QueryCondition>& conditions, 
                                   bool use_and = true, size_t limit = 0);
    
    // 聚合查询
    AggregateResult count_all();
    AggregateResult count_where(const QueryCondition& condition);
    AggregateResult sum_values(const std::string& key_pattern = "");
    AggregateResult avg_values(const std::string& key_pattern = "");
    AggregateResult min_max_values(const std::string& key_pattern = "");
    
    // 排序查询
    QueryResult scan_ordered(const std::string& start_key = "", 
                           const std::string& end_key = "", 
                           SortOrder order = SortOrder::ASC, 
                           size_t limit = 0);
    QueryResult prefix_scan_ordered(const std::string& prefix, 
                                  SortOrder order = SortOrder::ASC, 
                                  size_t limit = 0);
    
    // 复合查询（条件 + 排序）
    QueryResult query_where_ordered(const QueryCondition& condition,
                                  SortOrder order = SortOrder::ASC,
                                  size_t limit = 0);

private:
    KVDB& db_;
    
    // 辅助方法
    bool evaluate_condition(const std::string& key, const std::string& value, 
                          const QueryCondition& condition);
    bool match_pattern(const std::string& text, const std::string& pattern);
    double parse_numeric_value(const std::string& value);
    bool is_numeric(const std::string& value);
    
    // 排序辅助
    void sort_results(std::vector<std::pair<std::string, std::string>>& results, 
                     SortOrder order);
    
    // 迭代器辅助
    std::unique_ptr<Iterator> create_iterator();
    void collect_results(Iterator* iter, 
                        std::vector<std::pair<std::string, std::string>>& results,
                        const std::vector<QueryCondition>& conditions = {},
                        bool use_and = true,
                        size_t limit = 0);
};