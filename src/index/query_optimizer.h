#pragma once
#include "index_types.h"
#include "index_manager.h"
#include <memory>

// 前向声明
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

struct QueryCondition {
    std::string field;  // "key" 或 "value"
    ConditionOperator op;
    std::string value;
    
    QueryCondition(const std::string& f, ConditionOperator o, const std::string& v)
        : field(f), op(o), value(v) {}
};

class QueryOptimizer {
public:
    // 查询计划
    struct QueryPlan {
        bool use_index;
        std::string index_name;
        IndexQuery index_query;
        std::vector<std::string> candidate_keys;
        double estimated_cost;
        double estimated_selectivity;
        
        QueryPlan() : use_index(false), estimated_cost(0.0), estimated_selectivity(0.0) {}
    };
    
    // 执行策略
    enum class ExecutionStrategy {
        FULL_SCAN,           // 全表扫描
        INDEX_LOOKUP,        // 索引查找
        INDEX_RANGE_SCAN,    // 索引范围扫描
        COMPOSITE_INDEX,     // 复合索引
        FULLTEXT_SEARCH,     // 全文搜索
        INVERTED_INDEX       // 倒排索引
    };
    
    explicit QueryOptimizer(IndexManager& index_manager);
    ~QueryOptimizer();
    
    // 查询优化
    QueryPlan optimize_single_condition(const QueryCondition& condition);
    QueryPlan optimize_multiple_conditions(const std::vector<QueryCondition>& conditions, bool use_and);
    
    // 执行策略选择
    ExecutionStrategy choose_strategy(const QueryPlan& plan);
    
    // 成本估算
    double estimate_full_scan_cost(size_t total_records);
    double estimate_index_lookup_cost(const std::string& index_name, const IndexQuery& query);
    
    // 选择性估算
    double estimate_condition_selectivity(const QueryCondition& condition);
    double estimate_index_selectivity(const std::string& index_name, const IndexQuery& query);
    
    // 查询重写
    std::vector<QueryCondition> rewrite_conditions(const std::vector<QueryCondition>& conditions);
    
    // 索引推荐
    struct IndexRecommendation {
        std::string suggested_name;
        IndexType type;
        std::vector<std::string> fields;
        double expected_improvement;
        std::string reason;
        
        IndexRecommendation(const std::string& name, IndexType t, 
                           const std::vector<std::string>& f, double improvement, 
                           const std::string& r)
            : suggested_name(name), type(t), fields(f), 
              expected_improvement(improvement), reason(r) {}
    };
    
    std::vector<IndexRecommendation> recommend_indexes(const std::vector<QueryCondition>& frequent_conditions);
    
    // 统计信息
    struct OptimizerStats {
        size_t total_queries;
        size_t index_hits;
        size_t full_scans;
        double average_query_time;
        double index_hit_rate;
        
        OptimizerStats() : total_queries(0), index_hits(0), full_scans(0), 
                          average_query_time(0.0), index_hit_rate(0.0) {}
    };
    
    OptimizerStats get_stats() const { return stats_; }
    void reset_stats() { stats_ = OptimizerStats(); }
    
private:
    IndexManager& index_manager_;
    mutable OptimizerStats stats_;
    
    // 辅助方法
    QueryType condition_to_query_type(const QueryCondition& condition);
    IndexQuery condition_to_index_query(const QueryCondition& condition);
    bool can_use_index_for_condition(const std::string& index_name, const QueryCondition& condition);
    
    // 成本模型参数
    static constexpr double FULL_SCAN_COST_PER_RECORD = 1.0;
    static constexpr double INDEX_LOOKUP_BASE_COST = 10.0;
    static constexpr double INDEX_SCAN_COST_PER_RECORD = 0.1;
    
    // 选择性估算参数
    static constexpr double DEFAULT_EQUALITY_SELECTIVITY = 0.1;
    static constexpr double DEFAULT_RANGE_SELECTIVITY = 0.3;
    static constexpr double DEFAULT_LIKE_SELECTIVITY = 0.2;
};