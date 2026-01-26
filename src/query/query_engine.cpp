#include "query/query_engine.h"
#include "iterator/merge_iterator.h"
#include <algorithm>
#include <sstream>
#include <regex>
#include <cmath>
#include <iostream>

QueryEngine::QueryEngine(KVDB& db) : db_(db) {}

// 批量操作实现
bool QueryEngine::batch_put(const std::vector<std::pair<std::string, std::string>>& pairs) {
    try {
        for (const auto& pair : pairs) {
            if (!db_.put(pair.first, pair.second)) {
                return false;
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Batch PUT error: " << e.what() << std::endl;
        return false;
    }
}

QueryResult QueryEngine::batch_get(const std::vector<std::string>& keys) {
    QueryResult result;
    
    try {
        for (const std::string& key : keys) {
            std::string value;
            if (db_.get(key, value)) {
                result.results.emplace_back(key, value);
            }
        }
        result.total_count = result.results.size();
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }
    
    return result;
}

bool QueryEngine::batch_delete(const std::vector<std::string>& keys) {
    try {
        for (const std::string& key : keys) {
            if (!db_.del(key)) {
                return false;
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Batch DELETE error: " << e.what() << std::endl;
        return false;
    }
}

// 条件查询实现
QueryResult QueryEngine::query_where(const QueryCondition& condition, size_t limit) {
    return query_where_multiple({condition}, true, limit);
}

QueryResult QueryEngine::query_where_multiple(const std::vector<QueryCondition>& conditions, 
                                             bool use_and, size_t limit) {
    QueryResult result;
    
    try {
        auto iter = create_iterator();
        collect_results(iter.get(), result.results, conditions, use_and, limit);
        result.total_count = result.results.size();
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }
    
    return result;
}

// 聚合查询实现
AggregateResult QueryEngine::count_all() {
    AggregateResult result;
    
    try {
        auto iter = create_iterator();
        iter->seek_to_first();
        
        while (iter->valid()) {
            result.count++;
            iter->next();
        }
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }
    
    return result;
}

AggregateResult QueryEngine::count_where(const QueryCondition& condition) {
    AggregateResult result;
    
    try {
        auto iter = create_iterator();
        iter->seek_to_first();
        
        while (iter->valid()) {
            std::string key = iter->key();
            std::string value = iter->value();
            
            if (evaluate_condition(key, value, condition)) {
                result.count++;
            }
            iter->next();
        }
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }
    
    return result;
}

AggregateResult QueryEngine::sum_values(const std::string& key_pattern) {
    AggregateResult result;
    
    try {
        auto iter = create_iterator();
        iter->seek_to_first();
        
        while (iter->valid()) {
            std::string key = iter->key();
            std::string value = iter->value();
            
            // 检查键是否匹配模式
            if (key_pattern.empty() || match_pattern(key, key_pattern)) {
                if (is_numeric(value)) {
                    double num_value = parse_numeric_value(value);
                    result.sum += num_value;
                    result.count++;
                }
            }
            iter->next();
        }
        
        result.avg = (result.count > 0) ? result.sum / result.count : 0.0;
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }
    
    return result;
}

AggregateResult QueryEngine::avg_values(const std::string& key_pattern) {
    return sum_values(key_pattern);  // sum_values 已经计算了平均值
}

AggregateResult QueryEngine::min_max_values(const std::string& key_pattern) {
    AggregateResult result;
    bool first = true;
    
    try {
        auto iter = create_iterator();
        iter->seek_to_first();
        
        while (iter->valid()) {
            std::string key = iter->key();
            std::string value = iter->value();
            
            // 检查键是否匹配模式
            if (key_pattern.empty() || match_pattern(key, key_pattern)) {
                if (is_numeric(value)) {
                    double num_value = parse_numeric_value(value);
                    
                    if (first) {
                        result.min = result.max = num_value;
                        first = false;
                    } else {
                        result.min = std::min(result.min, num_value);
                        result.max = std::max(result.max, num_value);
                    }
                    result.count++;
                    result.sum += num_value;
                }
            }
            iter->next();
        }
        
        result.avg = (result.count > 0) ? result.sum / result.count : 0.0;
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }
    
    return result;
}

// 排序查询实现
QueryResult QueryEngine::scan_ordered(const std::string& start_key, 
                                     const std::string& end_key, 
                                     SortOrder order, 
                                     size_t limit) {
    QueryResult result;
    
    try {
        auto iter = create_iterator();
        
        if (start_key.empty()) {
            iter->seek_to_first();
        } else {
            iter->seek(start_key);
        }
        
        while (iter->valid()) {
            std::string key = iter->key();
            std::string value = iter->value();
            
            // 检查是否超出结束键（只有当end_key不为空时才检查）
            if (!end_key.empty() && key > end_key) {
                break;
            }
            
            // 添加所有记录（包括空值）
            result.results.emplace_back(key, value);
            
            // 检查限制
            if (limit > 0 && result.results.size() >= limit) {
                break;
            }
            
            iter->next();
        }
        
        // 排序结果
        if (order == SortOrder::DESC) {
            sort_results(result.results, order);
        }
        // ASC order is already natural from iterator
        
        result.total_count = result.results.size();
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }
    
    return result;
}

QueryResult QueryEngine::prefix_scan_ordered(const std::string& prefix, 
                                            SortOrder order, 
                                            size_t limit) {
    QueryResult result;
    
    try {
        Snapshot snap = db_.get_snapshot();
        auto iter = db_.new_prefix_iterator(snap, prefix);
        
        while (iter->valid()) {
            std::string key = iter->key();
            std::string value = iter->value();
            
            // 检查是否还在前缀范围内
            if (!(key.size() >= prefix.size() && 
                  key.substr(0, prefix.size()) == prefix)) {
                break;
            }
            
            result.results.emplace_back(key, value);
            
            // 检查限制
            if (limit > 0 && result.results.size() >= limit) {
                break;
            }
            
            iter->next();
        }
        
        // 排序结果
        if (order == SortOrder::DESC) {
            sort_results(result.results, order);
        }
        
        result.total_count = result.results.size();
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }
    
    return result;
}

// 复合查询实现
QueryResult QueryEngine::query_where_ordered(const QueryCondition& condition,
                                            SortOrder order,
                                            size_t limit) {
    QueryResult result = query_where(condition, 0);  // 先不限制数量
    
    if (result.success) {
        // 排序结果
        sort_results(result.results, order);
        
        // 应用限制
        if (limit > 0 && result.results.size() > limit) {
            result.results.resize(limit);
            result.total_count = limit;
        }
    }
    
    return result;
}

// 辅助方法实现
bool QueryEngine::evaluate_condition(const std::string& key, const std::string& value, 
                                    const QueryCondition& condition) {
    std::string target = (condition.field == "key") ? key : value;
    
    switch (condition.op) {
        case ConditionOperator::EQUALS:
            return target == condition.value;
            
        case ConditionOperator::NOT_EQUALS:
            return target != condition.value;
            
        case ConditionOperator::LIKE:
            return match_pattern(target, condition.value);
            
        case ConditionOperator::NOT_LIKE:
            return !match_pattern(target, condition.value);
            
        case ConditionOperator::GREATER_THAN:
            if (is_numeric(target) && is_numeric(condition.value)) {
                return parse_numeric_value(target) > parse_numeric_value(condition.value);
            }
            return target > condition.value;
            
        case ConditionOperator::LESS_THAN:
            if (is_numeric(target) && is_numeric(condition.value)) {
                return parse_numeric_value(target) < parse_numeric_value(condition.value);
            }
            return target < condition.value;
            
        case ConditionOperator::GREATER_EQUAL:
            if (is_numeric(target) && is_numeric(condition.value)) {
                return parse_numeric_value(target) >= parse_numeric_value(condition.value);
            }
            return target >= condition.value;
            
        case ConditionOperator::LESS_EQUAL:
            if (is_numeric(target) && is_numeric(condition.value)) {
                return parse_numeric_value(target) <= parse_numeric_value(condition.value);
            }
            return target <= condition.value;
    }
    
    return false;
}

bool QueryEngine::match_pattern(const std::string& text, const std::string& pattern) {
    try {
        // 如果模式为空，匹配所有
        if (pattern.empty()) {
            return true;
        }
        
        // 简单的通配符匹配，支持 * 和 ?
        if (pattern.find('*') != std::string::npos || pattern.find('?') != std::string::npos) {
            // 将通配符模式转换为正则表达式
            std::string regex_pattern = pattern;
            
            // 转义特殊字符（除了*和?）
            std::string special_chars = "()[]{}+.^$|\\:";
            for (char c : special_chars) {
                std::string from(1, c);
                std::string to = "\\" + from;
                size_t pos = 0;
                while ((pos = regex_pattern.find(from, pos)) != std::string::npos) {
                    regex_pattern.replace(pos, 1, to);
                    pos += 2;
                }
            }
            
            // 替换通配符
            size_t pos = 0;
            while ((pos = regex_pattern.find("*", pos)) != std::string::npos) {
                regex_pattern.replace(pos, 1, ".*");
                pos += 2;
            }
            
            pos = 0;
            while ((pos = regex_pattern.find("?", pos)) != std::string::npos) {
                regex_pattern.replace(pos, 1, ".");
                pos += 1;
            }
            
            std::regex regex(regex_pattern);
            return std::regex_match(text, regex);
        } else {
            // 没有通配符，使用前缀匹配
            return text.find(pattern) == 0;
        }
    } catch (const std::exception&) {
        // 如果正则表达式失败，回退到简单的前缀匹配
        return text.find(pattern) == 0;
    }
}

double QueryEngine::parse_numeric_value(const std::string& value) {
    try {
        std::string clean_value = value;
        // Remove surrounding quotes if present
        if (clean_value.size() >= 2 && clean_value.front() == '"' && clean_value.back() == '"') {
            clean_value = clean_value.substr(1, clean_value.size() - 2);
        }
        return std::stod(clean_value);
    } catch (const std::exception&) {
        return 0.0;
    }
}

bool QueryEngine::is_numeric(const std::string& value) {
    if (value.empty()) return false;
    
    try {
        std::string clean_value = value;
        // Remove surrounding quotes if present
        if (clean_value.size() >= 2 && clean_value.front() == '"' && clean_value.back() == '"') {
            clean_value = clean_value.substr(1, clean_value.size() - 2);
        }
        std::stod(clean_value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void QueryEngine::sort_results(std::vector<std::pair<std::string, std::string>>& results, 
                              SortOrder order) {
    if (order == SortOrder::ASC) {
        std::sort(results.begin(), results.end(),
                 [](const auto& a, const auto& b) {
                     return a.first < b.first;
                 });
    } else {
        std::sort(results.begin(), results.end(),
                 [](const auto& a, const auto& b) {
                     return a.first > b.first;
                 });
    }
}

std::unique_ptr<Iterator> QueryEngine::create_iterator() {
    Snapshot snap = db_.get_snapshot();
    return db_.new_iterator(snap);
}

void QueryEngine::collect_results(Iterator* iter, 
                                std::vector<std::pair<std::string, std::string>>& results,
                                const std::vector<QueryCondition>& conditions,
                                bool use_and,
                                size_t limit) {
    iter->seek_to_first();
    
    while (iter->valid()) {
        std::string key = iter->key();
        std::string value = iter->value();
        
        bool matches = true;
        
        if (!conditions.empty()) {
            if (use_and) {
                // AND 逻辑：所有条件都必须满足
                for (const auto& condition : conditions) {
                    if (!evaluate_condition(key, value, condition)) {
                        matches = false;
                        break;
                    }
                }
            } else {
                // OR 逻辑：至少一个条件满足
                matches = false;
                for (const auto& condition : conditions) {
                    if (evaluate_condition(key, value, condition)) {
                        matches = true;
                        break;
                    }
                }
            }
        }
        
        if (matches) {
            results.emplace_back(key, value);
            
            // 检查限制
            if (limit > 0 && results.size() >= limit) {
                break;
            }
        }
        
        iter->next();
    }
}