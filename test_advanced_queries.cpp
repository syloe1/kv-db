#include "db/kv_db.h"
#include "query/query_engine.h"
#include <iostream>
#include <cassert>

void test_batch_operations() {
    std::cout << "=== 测试批量操作 ===\n";
    
    KVDB db("test_batch.wal");
    QueryEngine engine(db);
    
    // 测试批量PUT
    std::vector<std::pair<std::string, std::string>> batch_data = {
        {"user:1", "Alice"},
        {"user:2", "Bob"},
        {"user:3", "Charlie"},
        {"score:1", "95"},
        {"score:2", "87"},
        {"score:3", "92"}
    };
    
    assert(engine.batch_put(batch_data));
    std::cout << "✓ 批量PUT成功\n";
    
    // 测试批量GET
    std::vector<std::string> keys = {"user:1", "user:2", "user:3", "nonexistent"};
    QueryResult result = engine.batch_get(keys);
    
    assert(result.success);
    assert(result.results.size() == 3);  // 3个存在的键
    std::cout << "✓ 批量GET成功，找到 " << result.results.size() << " 个键\n";
    
    // 测试批量DELETE
    std::vector<std::string> del_keys = {"user:2", "score:2"};
    assert(engine.batch_delete(del_keys));
    std::cout << "✓ 批量DELETE成功\n";
    
    // 验证删除
    std::string value;
    assert(!db.get("user:2", value));
    assert(!db.get("score:2", value));
    std::cout << "✓ 删除验证成功\n";
}

void test_conditional_queries() {
    std::cout << "\n=== 测试条件查询 ===\n";
    
    KVDB db("test_condition.wal");
    QueryEngine engine(db);
    
    // 准备测试数据
    db.put("product:1", "laptop");
    db.put("product:2", "mouse");
    db.put("product:3", "keyboard");
    db.put("price:1", "1200");
    db.put("price:2", "25");
    db.put("price:3", "80");
    db.put("category:electronics", "active");
    
    // 测试LIKE查询
    QueryCondition like_condition("key", ConditionOperator::LIKE, "product:*");
    QueryResult result = engine.query_where(like_condition);
    
    assert(result.success);
    assert(result.results.size() == 3);
    std::cout << "✓ LIKE查询成功，找到 " << result.results.size() << " 个产品\n";
    
    // 测试数值比较
    QueryCondition gt_condition("value", ConditionOperator::GREATER_THAN, "50");
    result = engine.query_where(gt_condition);
    
    assert(result.success);
    std::cout << "✓ 数值比较查询成功，找到 " << result.results.size() << " 个结果\n";
    
    // 测试等值查询
    QueryCondition eq_condition("value", ConditionOperator::EQUALS, "active");
    result = engine.query_where(eq_condition);
    
    assert(result.success);
    assert(result.results.size() == 1);
    std::cout << "✓ 等值查询成功\n";
}

void test_aggregate_queries() {
    std::cout << "\n=== 测试聚合查询 ===\n";
    
    KVDB db("test_aggregate.wal");
    QueryEngine engine(db);
    
    // 准备数值测试数据
    db.put("score:math:1", "95");
    db.put("score:math:2", "87");
    db.put("score:math:3", "92");
    db.put("score:english:1", "88");
    db.put("score:english:2", "91");
    db.put("temperature:1", "23.5");
    db.put("temperature:2", "25.0");
    db.put("temperature:3", "22.8");
    
    // 测试COUNT
    AggregateResult count_result = engine.count_all();
    assert(count_result.success);
    assert(count_result.count == 8);
    std::cout << "✓ COUNT查询成功，总记录数: " << count_result.count << "\n";
    
    // 测试SUM和AVG（数学成绩）
    AggregateResult sum_result = engine.sum_values("score:math:*");
    assert(sum_result.success);
    assert(sum_result.count == 3);
    std::cout << "✓ SUM查询成功，数学成绩总和: " << sum_result.sum 
              << "，平均分: " << sum_result.avg << "\n";
    
    // 测试MIN/MAX（温度）
    AggregateResult minmax_result = engine.min_max_values("temperature:*");
    assert(minmax_result.success);
    assert(minmax_result.count == 3);
    std::cout << "✓ MIN/MAX查询成功，温度范围: " << minmax_result.min 
              << " - " << minmax_result.max << "°C\n";
}

void test_ordered_queries() {
    std::cout << "\n=== 测试排序查询 ===\n";
    
    KVDB db("test_ordered.wal");
    QueryEngine engine(db);
    
    // 准备测试数据
    db.put("item:c", "third");
    db.put("item:a", "first");
    db.put("item:b", "second");
    db.put("item:d", "fourth");
    
    // 测试升序扫描
    QueryResult asc_result = engine.scan_ordered("item:", "item:z", SortOrder::ASC);
    assert(asc_result.success);
    assert(asc_result.results.size() == 4);
    assert(asc_result.results[0].first == "item:a");
    assert(asc_result.results[3].first == "item:d");
    std::cout << "✓ 升序扫描成功\n";
    
    // 测试降序扫描
    QueryResult desc_result = engine.scan_ordered("item:", "item:z", SortOrder::DESC);
    assert(desc_result.success);
    assert(desc_result.results.size() == 4);
    assert(desc_result.results[0].first == "item:d");
    assert(desc_result.results[3].first == "item:a");
    std::cout << "✓ 降序扫描成功\n";
    
    // 测试带限制的查询
    QueryResult limited_result = engine.scan_ordered("", "", SortOrder::ASC, 2);
    assert(limited_result.success);
    assert(limited_result.results.size() == 2);
    std::cout << "✓ 限制数量查询成功，返回 " << limited_result.results.size() << " 条记录\n";
}

void test_complex_queries() {
    std::cout << "\n=== 测试复合查询 ===\n";
    
    KVDB db("test_complex.wal");
    QueryEngine engine(db);
    
    // 准备测试数据
    db.put("employee:1:name", "Alice");
    db.put("employee:1:salary", "5000");
    db.put("employee:2:name", "Bob");
    db.put("employee:2:salary", "6000");
    db.put("employee:3:name", "Charlie");
    db.put("employee:3:salary", "4500");
    
    // 测试条件+排序查询
    QueryCondition salary_condition("key", ConditionOperator::LIKE, "*:salary");
    QueryResult result = engine.query_where_ordered(salary_condition, SortOrder::DESC);
    
    assert(result.success);
    assert(result.results.size() == 3);
    std::cout << "✓ 条件+排序查询成功，找到 " << result.results.size() << " 个薪资记录\n";
    
    // 验证排序（按键降序）
    assert(result.results[0].first > result.results[1].first);
    std::cout << "✓ 排序验证成功\n";
}

int main() {
    try {
        test_batch_operations();
        test_conditional_queries();
        test_aggregate_queries();
        test_ordered_queries();
        test_complex_queries();
        
        std::cout << "\n🎉 所有高级查询功能测试通过！\n";
        
        // 清理测试文件
        std::system("rm -f test_*.wal");
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试失败: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}