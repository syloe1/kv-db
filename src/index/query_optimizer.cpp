#include "query_optimizer.h"
#include <algorithm>
#include <cmath>

QueryOptimizer::QueryOptimizer(IndexManager& index_manager) 
    : index_manager_(index_manager) {
}

QueryOptimizer::~QueryOptimizer() = default;

QueryOptimizer::QueryPlan QueryOptimizer::optimize_single_condition(const QueryCondition& condition) {
    QueryPlan plan;
    stats_.total_queries++;
    
    // 转换查询条件为索引查询类型
    QueryType query_type = condition_to_query_type(condition);
    
    // 查找适用的索引
    std::vector<std::string> applicable_indexes = index_manager_.get_applicable_indexes(condition.field, query_type);
    
    if (applicable_indexes.empty()) {
        // 没有适用的索引，使用全表扫描
        plan.use_index = false;
        plan.estimated_cost = estimate_full_scan_cost(10000); // 假设10000条记录
        plan.estimated_selectivity = estimate_condition_selectivity(condition);
        stats_.full_scans++;
        return plan;
    }
    
    // 选择最佳索引
    std::string best_index;
    double best_cost = std::numeric_limits<double>::max();
    
    IndexQuery index_query = condition_to_index_query(condition);
    
    for (const std::string& index_name : applicable_indexes) {
        double cost = estimate_index_lookup_cost(index_name, index_query);
        if (cost < best_cost) {
            best_cost = cost;
            best_index = index_name;
        }
    }
    
    // 比较索引查找和全表扫描的成本
    double full_scan_cost = estimate_full_scan_cost(10000);
    
    if (best_cost < full_scan_cost) {
        plan.use_index = true;
        plan.index_name = best_index;
        plan.index_query = index_query;
        plan.estimated_cost = best_cost;
        plan.estimated_selectivity = estimate_index_selectivity(best_index, index_query);
        stats_.index_hits++;
        
        // 执行索引查找获取候选键
        IndexLookupResult lookup_result = index_manager_.lookup(best_index, index_query);
        if (lookup_result.success) {
            plan.candidate_keys = lookup_result.keys;
        }
    } else {
        plan.use_index = false;
        plan.estimated_cost = full_scan_cost;
        plan.estimated_selectivity = estimate_condition_selectivity(condition);
        stats_.full_scans++;
    }
    
    return plan;
}

QueryOptimizer::QueryPlan QueryOptimizer::optimize_multiple_conditions(const std::vector<QueryCondition>& conditions, bool use_and) {
    QueryPlan plan;
    
    if (conditions.empty()) {
        return plan;
    }
    
    if (conditions.size() == 1) {
        return optimize_single_condition(conditions[0]);
    }
    
    // 对于多条件查询，尝试找到最优的索引组合
    std::vector<QueryPlan> individual_plans;
    for (const QueryCondition& condition : conditions) {
        individual_plans.push_back(optimize_single_condition(condition));
    }
    
    if (use_and) {
        // AND 查询：选择选择性最高的索引
        QueryPlan* best_plan = nullptr;
        double best_selectivity = 1.0;
        
        for (QueryPlan& individual_plan : individual_plans) {
            if (individual_plan.use_index && individual_plan.estimated_selectivity < best_selectivity) {
                best_selectivity = individual_plan.estimated_selectivity;
                best_plan = &individual_plan;
            }
        }
        
        if (best_plan) {
            plan = *best_plan;
            // 调整选择性估算（多个条件的交集）
            plan.estimated_selectivity = best_selectivity;
            for (const QueryPlan& other_plan : individual_plans) {
                if (&other_plan != best_plan) {
                    plan.estimated_selectivity *= other_plan.estimated_selectivity;
                }
            }
        } else {
            // 没有可用索引，使用全表扫描
            plan.use_index = false;
            plan.estimated_cost = estimate_full_scan_cost(10000);
            plan.estimated_selectivity = DEFAULT_EQUALITY_SELECTIVITY;
            for (const QueryCondition& condition : conditions) {
                plan.estimated_selectivity *= estimate_condition_selectivity(condition);
            }
        }
    } else {
        // OR 查询：需要合并多个索引的结果
        plan.use_index = false;
        plan.estimated_cost = estimate_full_scan_cost(10000);
        
        // OR 查询的选择性是各条件选择性的并集
        double combined_selectivity = 0.0;
        for (const QueryCondition& condition : conditions) {
            combined_selectivity += estimate_condition_selectivity(condition);
        }
        plan.estimated_selectivity = std::min(1.0, combined_selectivity);
    }
    
    return plan;
}

QueryOptimizer::ExecutionStrategy QueryOptimizer::choose_strategy(const QueryPlan& plan) {
    if (!plan.use_index) {
        return ExecutionStrategy::FULL_SCAN;
    }
    
    // 根据索引类型选择执行策略
    std::vector<IndexMetadata> indexes = index_manager_.list_indexes();
    
    for (const IndexMetadata& metadata : indexes) {
        if (metadata.name == plan.index_name) {
            switch (metadata.type) {
                case IndexType::SECONDARY:
                    if (plan.index_query.type == QueryType::RANGE_QUERY) {
                        return ExecutionStrategy::INDEX_RANGE_SCAN;
                    } else {
                        return ExecutionStrategy::INDEX_LOOKUP;
                    }
                case IndexType::COMPOSITE:
                    return ExecutionStrategy::COMPOSITE_INDEX;
                case IndexType::FULLTEXT:
                    return ExecutionStrategy::FULLTEXT_SEARCH;
                case IndexType::INVERTED:
                    return ExecutionStrategy::INVERTED_INDEX;
            }
        }
    }
    
    return ExecutionStrategy::INDEX_LOOKUP;
}

double QueryOptimizer::estimate_full_scan_cost(size_t total_records) {
    return total_records * FULL_SCAN_COST_PER_RECORD;
}

double QueryOptimizer::estimate_index_lookup_cost(const std::string& index_name, const IndexQuery& query) {
    IndexStats stats = index_manager_.get_index_stats(index_name);
    
    double base_cost = INDEX_LOOKUP_BASE_COST;
    double scan_cost = 0.0;
    
    switch (query.type) {
        case QueryType::EXACT_MATCH:
            // 精确匹配：基础成本 + 少量扫描成本
            scan_cost = INDEX_SCAN_COST_PER_RECORD * (1.0 / stats.selectivity);
            break;
        case QueryType::RANGE_QUERY:
            // 范围查询：基础成本 + 更多扫描成本
            scan_cost = INDEX_SCAN_COST_PER_RECORD * (stats.total_entries * DEFAULT_RANGE_SELECTIVITY);
            break;
        case QueryType::PREFIX_MATCH:
            // 前缀匹配：中等扫描成本
            scan_cost = INDEX_SCAN_COST_PER_RECORD * (stats.total_entries * DEFAULT_LIKE_SELECTIVITY);
            break;
        default:
            scan_cost = INDEX_SCAN_COST_PER_RECORD * stats.total_entries * 0.1;
            break;
    }
    
    return base_cost + scan_cost;
}

double QueryOptimizer::estimate_condition_selectivity(const QueryCondition& condition) {
    switch (condition.op) {
        case ConditionOperator::EQUALS:
        case ConditionOperator::NOT_EQUALS:
            return DEFAULT_EQUALITY_SELECTIVITY;
        case ConditionOperator::GREATER_THAN:
        case ConditionOperator::LESS_THAN:
        case ConditionOperator::GREATER_EQUAL:
        case ConditionOperator::LESS_EQUAL:
            return DEFAULT_RANGE_SELECTIVITY;
        case ConditionOperator::LIKE:
        case ConditionOperator::NOT_LIKE:
            return DEFAULT_LIKE_SELECTIVITY;
        default:
            return 0.5; // 默认选择性
    }
}

double QueryOptimizer::estimate_index_selectivity(const std::string& index_name, const IndexQuery& query) {
    IndexStats stats = index_manager_.get_index_stats(index_name);
    
    switch (query.type) {
        case QueryType::EXACT_MATCH:
            return 1.0 / stats.unique_values;
        case QueryType::RANGE_QUERY:
            return DEFAULT_RANGE_SELECTIVITY;
        case QueryType::PREFIX_MATCH:
            return DEFAULT_LIKE_SELECTIVITY;
        default:
            return stats.selectivity;
    }
}

std::vector<QueryCondition> QueryOptimizer::rewrite_conditions(const std::vector<QueryCondition>& conditions) {
    std::vector<QueryCondition> rewritten = conditions;
    
    // 简单的查询重写：按选择性排序条件
    std::sort(rewritten.begin(), rewritten.end(),
              [this](const QueryCondition& a, const QueryCondition& b) {
                  return estimate_condition_selectivity(a) < estimate_condition_selectivity(b);
              });
    
    return rewritten;
}

std::vector<QueryOptimizer::IndexRecommendation> QueryOptimizer::recommend_indexes(const std::vector<QueryCondition>& frequent_conditions) {
    std::vector<IndexRecommendation> recommendations;
    
    // 统计字段使用频率
    std::map<std::string, size_t> field_frequency;
    std::map<ConditionOperator, size_t> operator_frequency;
    
    for (const QueryCondition& condition : frequent_conditions) {
        field_frequency[condition.field]++;
        operator_frequency[condition.op]++;
    }
    
    // 为高频字段推荐二级索引
    for (const auto& pair : field_frequency) {
        const std::string& field = pair.first;
        size_t frequency = pair.second;
        
        if (frequency >= 5) { // 阈值：出现5次以上
            // 检查是否已有该字段的索引
            std::vector<std::string> existing_indexes = index_manager_.get_applicable_indexes(field, QueryType::EXACT_MATCH);
            
            if (existing_indexes.empty()) {
                double improvement = frequency * 0.8; // 估算改进程度
                std::string reason = "Field '" + field + "' is frequently queried (" + std::to_string(frequency) + " times)";
                
                recommendations.emplace_back(
                    field + "_idx",
                    IndexType::SECONDARY,
                    std::vector<std::string>{field},
                    improvement,
                    reason
                );
            }
        }
    }
    
    // 为全文搜索推荐全文索引
    for (const QueryCondition& condition : frequent_conditions) {
        if (condition.op == ConditionOperator::LIKE && condition.field == "value") {
            std::vector<std::string> existing_indexes = index_manager_.get_applicable_indexes("value", QueryType::FULLTEXT_SEARCH);
            
            if (existing_indexes.empty()) {
                recommendations.emplace_back(
                    "value_fulltext_idx",
                    IndexType::FULLTEXT,
                    std::vector<std::string>{"value"},
                    10.0,
                    "LIKE queries on value field would benefit from full-text index"
                );
                break;
            }
        }
    }
    
    return recommendations;
}

QueryType QueryOptimizer::condition_to_query_type(const QueryCondition& condition) {
    switch (condition.op) {
        case ConditionOperator::EQUALS:
            return QueryType::EXACT_MATCH;
        case ConditionOperator::GREATER_THAN:
        case ConditionOperator::LESS_THAN:
        case ConditionOperator::GREATER_EQUAL:
        case ConditionOperator::LESS_EQUAL:
            return QueryType::RANGE_QUERY;
        case ConditionOperator::LIKE:
            return QueryType::PREFIX_MATCH;
        default:
            return QueryType::EXACT_MATCH;
    }
}

IndexQuery QueryOptimizer::condition_to_index_query(const QueryCondition& condition) {
    QueryType query_type = condition_to_query_type(condition);
    IndexQuery index_query(query_type, condition.field, condition.value);
    
    // 对于范围查询，需要设置范围参数
    if (query_type == QueryType::RANGE_QUERY) {
        switch (condition.op) {
            case ConditionOperator::GREATER_THAN:
            case ConditionOperator::GREATER_EQUAL:
                index_query.range_start = condition.value;
                index_query.range_end = "~"; // 表示最大值
                break;
            case ConditionOperator::LESS_THAN:
            case ConditionOperator::LESS_EQUAL:
                index_query.range_start = "";
                index_query.range_end = condition.value;
                break;
            default:
                break;
        }
    }
    
    return index_query;
}

bool QueryOptimizer::can_use_index_for_condition(const std::string& index_name, const QueryCondition& condition) {
    QueryType query_type = condition_to_query_type(condition);
    std::vector<std::string> applicable_indexes = index_manager_.get_applicable_indexes(condition.field, query_type);
    
    return std::find(applicable_indexes.begin(), applicable_indexes.end(), index_name) != applicable_indexes.end();
}