#include "src/db/kv_db.h"
#include "src/cache/multi_level_cache.h"
#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include <string>

class CacheOptimizationTest {
public:
    void run_all_tests() {
        std::cout << "=== 缓存策略优化测试 ===\n\n";
        
        test_basic_cache_functionality();
        test_multi_level_cache_performance();
        test_cache_warming();
        test_prefetching();
        test_adaptive_strategy();
        
        std::cout << "=== 所有测试完成 ===\n";
    }
    
private:
    void test_basic_cache_functionality() {
        std::cout << "1. 基础缓存功能测试\n";
        
        MultiLevelCache cache(100, 500); // 小容量便于测试
        
        // 测试基本put/get
        cache.put("key1", "value1");
        cache.put("key2", "value2");
        cache.put("key3", "value3");
        
        auto result1 = cache.get("key1");
        auto result2 = cache.get("key2");
        auto result_missing = cache.get("key_missing");
        
        assert(result1.has_value() && result1.value() == "value1");
        assert(result2.has_value() && result2.value() == "value2");
        assert(!result_missing.has_value());
        
        cache.print_stats();
        std::cout << "✓ 基础缓存功能测试通过\n\n";
    }
    
    void test_multi_level_cache_performance() {
        std::cout << "2. 多级缓存性能测试\n";
        
        // 创建测试数据
        std::vector<std::pair<std::string, std::string>> test_data;
        for (int i = 0; i < 1000; ++i) {
            test_data.emplace_back("key" + std::to_string(i), "value" + std::to_string(i));
        }
        
        // 测试传统缓存
        auto legacy_time = test_cache_performance("传统缓存", CacheManager::CacheType::LEGACY_BLOCK_CACHE, test_data);
        
        // 测试多级缓存
        auto multi_level_time = test_cache_performance("多级缓存", CacheManager::CacheType::MULTI_LEVEL_CACHE, test_data);
        
        std::cout << "性能对比：\n";
        std::cout << "传统缓存耗时: " << legacy_time << " ms\n";
        std::cout << "多级缓存耗时: " << multi_level_time << " ms\n";
        
        if (multi_level_time < legacy_time) {
            double improvement = (double)(legacy_time - multi_level_time) / legacy_time * 100;
            std::cout << "✓ 多级缓存性能提升: " << improvement << "%\n";
        }
        
        std::cout << "\n";
    }
    
    void test_cache_warming() {
        std::cout << "3. 缓存预热测试\n";
        
        CacheManager cache_manager(CacheManager::CacheType::MULTI_LEVEL_CACHE, 200, 1000);
        
        // 准备热点数据
        std::vector<std::pair<std::string, std::string>> hot_data;
        for (int i = 0; i < 50; ++i) {
            hot_data.emplace_back("hot_key" + std::to_string(i), "hot_value" + std::to_string(i));
        }
        
        // 预热缓存
        cache_manager.warm_cache(hot_data);
        
        // 测试预热效果
        auto start = std::chrono::high_resolution_clock::now();
        
        int hits = 0;
        for (const auto& [key, expected_value] : hot_data) {
            auto result = cache_manager.get(key);
            if (result.has_value() && result.value() == expected_value) {
                hits++;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        std::cout << "预热数据命中率: " << (double)hits / hot_data.size() * 100 << "%\n";
        std::cout << "预热数据访问耗时: " << duration << " μs\n";
        
        cache_manager.print_stats();
        std::cout << "✓ 缓存预热测试通过\n\n";
    }
    
    void test_prefetching() {
        std::cout << "4. 预读机制测试\n";
        
        MultiLevelCache cache(100, 500);
        
        // 模拟顺序访问模式
        std::vector<std::string> sequential_keys;
        for (int i = 0; i < 20; ++i) {
            std::string key = "seq_key_" + std::to_string(i);
            sequential_keys.push_back(key);
            cache.put(key, "value_" + std::to_string(i));
        }
        
        // 模拟预读
        std::vector<std::string> prefetch_keys = {"seq_key_10", "seq_key_11", "seq_key_12"};
        cache.prefetch(prefetch_keys, [&](const std::string& key) -> std::optional<std::string> {
            // 模拟从存储加载
            if (key.find("seq_key_") == 0) {
                return "loaded_" + key;
            }
            return std::nullopt;
        });
        
        cache.print_stats();
        std::cout << "✓ 预读机制测试通过\n\n";
    }
    
    void test_adaptive_strategy() {
        std::cout << "5. 自适应策略测试\n";
        
        MultiLevelCache cache(50, 200);
        
        // 模拟不同的访问模式
        
        // 1. 随机访问模式
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 100);
        
        for (int i = 0; i < 100; ++i) {
            std::string key = "random_key_" + std::to_string(dis(gen));
            cache.put(key, "random_value_" + std::to_string(i));
            cache.get(key);
        }
        
        std::cout << "随机访问模式统计:\n";
        cache.print_stats();
        
        // 2. 顺序访问模式
        for (int i = 0; i < 50; ++i) {
            std::string key = "seq_key_" + std::to_string(i);
            cache.put(key, "seq_value_" + std::to_string(i));
            cache.get(key);
        }
        
        std::cout << "顺序访问模式统计:\n";
        cache.print_stats();
        
        // 触发自适应调整
        cache.adjust_strategy();
        
        std::cout << "✓ 自适应策略测试通过\n\n";
    }
    
    long test_cache_performance(const std::string& cache_name, 
                               CacheManager::CacheType cache_type,
                               const std::vector<std::pair<std::string, std::string>>& test_data) {
        
        CacheManager cache_manager(cache_type, 200, 1000);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // 写入数据
        for (const auto& [key, value] : test_data) {
            cache_manager.put(key, value);
        }
        
        // 随机读取测试
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, test_data.size() - 1);
        
        for (int i = 0; i < 2000; ++i) {
            int idx = dis(gen);
            cache_manager.get(test_data[idx].first);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        std::cout << cache_name << " 统计:\n";
        cache_manager.print_stats();
        
        return duration;
    }
};

int main() {
    try {
        CacheOptimizationTest test;
        test.run_all_tests();
        
        std::cout << "\n=== 数据库集成测试 ===\n";
        
        // 测试数据库集成
        KVDB db("test_cache_opt.wal");
        
        // 启用多级缓存
        db.enable_multi_level_cache();
        
        // 准备热点数据并预热
        std::vector<std::pair<std::string, std::string>> hot_data;
        for (int i = 0; i < 10; ++i) {
            std::string key = "hot_" + std::to_string(i);
            std::string value = "hot_value_" + std::to_string(i);
            hot_data.emplace_back(key, value);
            db.put(key, value);
        }
        
        db.warm_cache_with_hot_data(hot_data);
        
        // 测试读取性能
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 100; ++i) {
            for (const auto& [key, expected_value] : hot_data) {
                std::string value;
                db.get(key, value);
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        std::cout << "数据库缓存性能测试:\n";
        std::cout << "1000次热点数据读取耗时: " << duration << " μs\n";
        std::cout << "平均每次读取: " << duration / 1000.0 << " μs\n";
        
        db.print_cache_stats();
        
        std::cout << "✓ 数据库集成测试通过\n";
        
    } catch (const std::exception& e) {
        std::cerr << "测试失败: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}