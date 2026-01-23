#include "src/db/kv_db.h"
#include "src/index/index_manager.h"
#include "src/index/query_optimizer.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <random>

void test_secondary_index() {
    std::cout << "\n=== 测试二级索引 ===\n";
    
    KVDB db("test_index.wal");
    
    // 插入测试数据
    std::cout << "插入测试数据...\n";
    for (int i = 0; i < 1000; ++i) {
        std::string key = "user_" + std::to_string(i);
        std::string value = "age_" + std::to_string(20 + (i % 50)); // 年龄20-69
        db.put(key, value);
    }
    
    // 创建二级索引
    std::cout << "创建value字段的二级索引...\n";
    bool success = db.create_secondary_index("age_index", "value", false);
    std::cout << "索引创建" << (success ? "成功" : "失败") << "\n";
    
    // 测试索引查询
    std::cout << "测试索引查询...\n";
    IndexManager& index_manager = db.get_index_manager();
    
    IndexQuery query(QueryType::EXACT_MATCH, "value", "age_25");
    IndexLookupResult result = index_manager.lookup("age_index", query);
    
    std::cout << "查询 age_25 的结果: " << result.keys.size() << " 条记录\n";
    std::cout << "查询时间: " << result.query_time_ms << " ms\n";
    
    // 范围查询测试
    IndexQuery range_query(QueryType::RANGE_QUERY, "value", "");
    range_query.range_start = "age_25";
    range_query.range_end = "age_30";
    
    IndexLookupResult range_result = index_manager.lookup("age_index", range_query);
    std::cout << "范围查询 age_25 到 age_30 的结果: " << range_result.keys.size() << " 条记录\n";
    
    // 显示索引统计
    IndexStats stats = index_manager.get_index_stats("age_index");
    std::cout << "索引统计:\n";
    std::cout << "  总条目数: " << stats.total_entries << "\n";
    std::cout << "  唯一值数: " << stats.unique_values << "\n";
    std::cout << "  选择性: " << stats.selectivity << "\n";
    std::cout << "  内存使用: " << stats.memory_bytes << " bytes\n";
}

void test_composite_index() {
    std::cout << "\n=== 测试复合索引 ===\n";
    
    KVDB db("test_composite.wal");
    
    // 插入测试数据
    std::cout << "插入测试数据...\n";
    for (int i = 0; i < 500; ++i) {
        std::string key = "product_" + std::to_string(i);
        std::string value = "category_" + std::to_string(i % 10) + "_price_" + std::to_string(100 + (i % 900));
        db.put(key, value);
    }
    
    // 创建复合索引 (key + value)
    std::cout << "创建复合索引...\n";
    std::vector<std::string> fields = {"key", "value"};
    bool success = db.create_composite_index("product_composite_index", fields);
    std::cout << "复合索引创建" << (success ? "成功" : "失败") << "\n";
    
    // 测试复合索引查询
    IndexManager& index_manager = db.get_index_manager();
    
    std::vector<std::string> search_values = {"product_100", "category_0_price_200"};
    IndexQuery query(QueryType::EXACT_MATCH, "", "");
    query.terms = search_values;
    
    IndexLookupResult result = index_manager.lookup("product_composite_index", query);
    std::cout << "复合查询结果: " << result.keys.size() << " 条记录\n";
    
    // 部分匹配查询
    std::vector<std::string> partial_values = {"product_200"};
    IndexQuery partial_query(QueryType::PREFIX_MATCH, "", "");
    partial_query.terms = partial_values;
    
    IndexLookupResult partial_result = index_manager.lookup("product_composite_index", partial_query);
    std::cout << "部分匹配查询结果: " << partial_result.keys.size() << " 条记录\n";
}

void test_fulltext_index() {
    std::cout << "\n=== 测试全文索引 ===\n";
    
    KVDB db("test_fulltext.wal");
    
    // 插入测试文档
    std::cout << "插入测试文档...\n";
    std::vector<std::string> documents = {
        "The quick brown fox jumps over the lazy dog",
        "A fast brown fox leaps over a sleeping dog",
        "The lazy dog sleeps under the tree",
        "Quick brown animals are very fast",
        "Dogs and foxes are common animals"
    };
    
    for (size_t i = 0; i < documents.size(); ++i) {
        std::string key = "doc_" + std::to_string(i);
        db.put(key, documents[i]);
    }
    
    // 创建全文索引
    std::cout << "创建全文索引...\n";
    bool success = db.create_fulltext_index("content_fulltext_index", "value");
    std::cout << "全文索引创建" << (success ? "成功" : "失败") << "\n";
    
    // 测试全文搜索
    IndexManager& index_manager = db.get_index_manager();
    
    IndexQuery query(QueryType::FULLTEXT_SEARCH, "value", "brown fox");
    IndexLookupResult result = index_manager.lookup("content_fulltext_index", query);
    
    std::cout << "搜索 'brown fox' 的结果: " << result.keys.size() << " 条记录\n";
    for (const std::string& key : result.keys) {
        std::cout << "  找到文档: " << key << "\n";
    }
    
    // 短语搜索
    IndexQuery phrase_query(QueryType::PHRASE_SEARCH, "value", "");
    phrase_query.terms = {"quick", "brown"};
    
    IndexLookupResult phrase_result = index_manager.lookup("content_fulltext_index", phrase_query);
    std::cout << "短语搜索 'quick brown' 的结果: " << phrase_result.keys.size() << " 条记录\n";
}

void test_inverted_index() {
    std::cout << "\n=== 测试倒排索引 ===\n";
    
    KVDB db("test_inverted.wal");
    
    // 插入测试文档
    std::cout << "插入测试文档...\n";
    std::vector<std::string> documents = {
        "machine learning algorithms are powerful",
        "deep learning neural networks",
        "artificial intelligence and machine learning",
        "natural language processing with neural networks",
        "computer vision using deep learning"
    };
    
    for (size_t i = 0; i < documents.size(); ++i) {
        std::string key = "article_" + std::to_string(i);
        db.put(key, documents[i]);
    }
    
    // 创建倒排索引
    std::cout << "创建倒排索引...\n";
    bool success = db.create_inverted_index("content_inverted_index", "value");
    std::cout << "倒排索引创建" << (success ? "成功" : "失败") << "\n";
    
    // 测试倒排索引搜索
    IndexManager& index_manager = db.get_index_manager();
    
    // 单词搜索
    IndexQuery query(QueryType::EXACT_MATCH, "value", "learning");
    IndexLookupResult result = index_manager.lookup("content_inverted_index", query);
    
    std::cout << "搜索 'learning' 的结果: " << result.keys.size() << " 条记录\n";
    
    // 多词AND搜索
    IndexQuery and_query(QueryType::FULLTEXT_SEARCH, "value", "");
    and_query.terms = {"machine", "learning"};
    
    IndexLookupResult and_result = index_manager.lookup("content_inverted_index", and_query);
    std::cout << "AND搜索 'machine' AND 'learning' 的结果: " << and_result.keys.size() << " 条记录\n";
    
    // 短语搜索
    IndexQuery phrase_query(QueryType::PHRASE_SEARCH, "value", "");
    phrase_query.terms = {"neural", "networks"};
    
    IndexLookupResult phrase_result = index_manager.lookup("content_inverted_index", phrase_query);
    std::cout << "短语搜索 'neural networks' 的结果: " << phrase_result.keys.size() << " 条记录\n";
}

void test_query_optimizer() {
    std::cout << "\n=== 测试查询优化器 ===\n";
    
    KVDB db("test_optimizer.wal");
    
    // 插入测试数据
    for (int i = 0; i < 1000; ++i) {
        std::string key = "item_" + std::to_string(i);
        std::string value = "category_" + std::to_string(i % 20) + "_status_active";
        db.put(key, value);
    }
    
    // 创建多个索引
    db.create_secondary_index("value_index", "value", false);
    db.create_composite_index("key_value_index", {"key", "value"});
    
    IndexManager& index_manager = db.get_index_manager();
    QueryOptimizer optimizer(index_manager);
    
    // 测试查询优化
    QueryCondition condition("value", ConditionOperator::EQUALS, "category_5_status_active");
    QueryOptimizer::QueryPlan plan = optimizer.optimize_single_condition(condition);
    
    std::cout << "查询优化结果:\n";
    std::cout << "  使用索引: " << (plan.use_index ? "是" : "否") << "\n";
    if (plan.use_index) {
        std::cout << "  选择的索引: " << plan.index_name << "\n";
        std::cout << "  估算成本: " << plan.estimated_cost << "\n";
        std::cout << "  估算选择性: " << plan.estimated_selectivity << "\n";
        std::cout << "  候选键数量: " << plan.candidate_keys.size() << "\n";
    }
    
    // 获取优化器统计
    QueryOptimizer::OptimizerStats stats = optimizer.get_stats();
    std::cout << "优化器统计:\n";
    std::cout << "  总查询数: " << stats.total_queries << "\n";
    std::cout << "  索引命中数: " << stats.index_hits << "\n";
    std::cout << "  全表扫描数: " << stats.full_scans << "\n";
    
    // 索引推荐
    std::vector<QueryCondition> frequent_conditions = {
        QueryCondition("value", ConditionOperator::LIKE, "category_%"),
        QueryCondition("key", ConditionOperator::EQUALS, "item_100"),
        QueryCondition("value", ConditionOperator::EQUALS, "status_active")
    };
    
    std::vector<QueryOptimizer::IndexRecommendation> recommendations = 
        optimizer.recommend_indexes(frequent_conditions);
    
    std::cout << "索引推荐 (" << recommendations.size() << " 个):\n";
    for (const auto& rec : recommendations) {
        std::cout << "  推荐索引: " << rec.suggested_name << "\n";
        std::cout << "    类型: " << static_cast<int>(rec.type) << "\n";
        std::cout << "    字段: ";
        for (const std::string& field : rec.fields) {
            std::cout << field << " ";
        }
        std::cout << "\n    预期改进: " << rec.expected_improvement << "\n";
        std::cout << "    原因: " << rec.reason << "\n";
    }
}

void performance_comparison() {
    std::cout << "\n=== 性能对比测试 ===\n";
    
    KVDB db("test_performance.wal");
    
    // 插入大量测试数据
    std::cout << "插入10000条测试数据...\n";
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10000; ++i) {
        std::string key = "record_" + std::to_string(i);
        std::string value = "type_" + std::to_string(i % 100) + "_priority_" + std::to_string(i % 10);
        db.put(key, value);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "数据插入完成，耗时: " << duration.count() << " ms\n";
    
    // 创建索引
    std::cout << "创建索引...\n";
    start = std::chrono::high_resolution_clock::now();
    db.create_secondary_index("type_index", "value", false);
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "索引创建完成，耗时: " << duration.count() << " ms\n";
    
    // 测试查询性能
    IndexManager& index_manager = db.get_index_manager();
    
    // 使用索引查询
    std::cout << "使用索引查询...\n";
    start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 100; ++i) {
        std::string search_value = "type_" + std::to_string(i % 100) + "_priority_5";
        IndexQuery query(QueryType::EXACT_MATCH, "value", search_value);
        IndexLookupResult result = index_manager.lookup("type_index", query);
    }
    
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "索引查询100次完成，耗时: " << duration.count() << " ms\n";
    
    // 显示索引统计
    std::vector<IndexMetadata> indexes = db.list_indexes();
    std::cout << "当前索引列表 (" << indexes.size() << " 个):\n";
    for (const IndexMetadata& metadata : indexes) {
        std::cout << "  索引名: " << metadata.name << "\n";
        std::cout << "    类型: " << static_cast<int>(metadata.type) << "\n";
        std::cout << "    字段: ";
        for (const std::string& field : metadata.fields) {
            std::cout << field << " ";
        }
        std::cout << "\n    内存使用: " << metadata.memory_usage << " bytes\n";
    }
}

int main() {
    std::cout << "KVDB 索引优化测试\n";
    std::cout << "==================\n";
    
    try {
        test_secondary_index();
        test_composite_index();
        test_fulltext_index();
        test_inverted_index();
        test_query_optimizer();
        performance_comparison();
        
        std::cout << "\n所有测试完成！\n";
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}