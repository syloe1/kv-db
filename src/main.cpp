#include <iostream>
<<<<<<< HEAD
#include <fstream>
#include "db/kv_db.h"
#include "iterator/iterator.h"
#include "cli/repl.h"
#include <cstdlib>
#include <chrono>
#include <thread>
#include <string>
#include <vector>

void test_v12() {
    std::cout << "=== Test 12: Manifest + VersionSet ===\n";
    
    // 清理之前的MANIFEST文件
    std::filesystem::remove("MANIFEST");
    
    // 创建新的KVDB实例
    KVDB db("manifest_test.log");
    
    // 写入足够多的数据触发flush（超过4MB）
    std::string large_value(5000, 'x'); // 5KB的值
    for (int i = 0; i < 1000; i++) {
        db.put("key_" + std::to_string(i), large_value);
    }
    
    std::cout << "数据写入完成，等待flush...\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // 检查MANIFEST文件是否存在
    if (std::filesystem::exists("MANIFEST")) {
        std::cout << "✅ MANIFEST 文件已创建\n";
        
        // 读取MANIFEST内容
        std::ifstream manifest("MANIFEST");
        std::string line;
        int add_count = 0;
        
        while (std::getline(manifest, line)) {
            if (line.find("ADD") != std::string::npos) {
                add_count++;
                std::cout << "MANIFEST记录: " << line << "\n";
            }
        }
        
        std::cout << "✅ 找到 " << add_count << " 条ADD记录\n";
        
        // 测试数据读取
        std::string value;
        if (db.get("key_0", value)) {
            std::cout << "✅ 数据读取成功: key_0 = " << value << "\n";
        } else {
            std::cout << "❌ 数据读取失败\n";
        }
        
    } else {
        std::cout << "❌ MANIFEST 文件未创建\n";
    }
    
    std::cout << "Manifest + VersionSet 测试: ✅\n";
}

void test_mvcc_snapshot() {
    std::cout << "\n=== Test MVCC + Snapshot ===\n";
    
    // 清理之前的文件
    std::filesystem::remove("MANIFEST");
    std::filesystem::remove("mvcc_test.log");
    
    // 创建新的KVDB实例
    KVDB db("mvcc_test.log");
    
    // 1. 写入初始数据
    db.put("a", "1");
    db.put("b", "2");
    std::cout << "写入初始数据: a=1, b=2\n";
    
    // 2. 创建 Snapshot
    Snapshot snap = db.get_snapshot();
    std::cout << "创建 Snapshot，seq=" << snap.seq << "\n";
    
    // 3. 继续写入新数据
    db.put("a", "3");
    db.put("b", "4");
    db.put("c", "5");
    std::cout << "写入新数据: a=3, b=4, c=5\n";
    
    // 4. 使用 Snapshot 读取（应该看到旧数据）
    std::string value;
    if (db.get("a", snap, value)) {
        std::cout << "✅ Snapshot 读取 a = " << value << " (期望: 1)\n";
        if (value != "1") {
            std::cout << "❌ 错误：Snapshot 应该看到旧值 1\n";
        }
    } else {
        std::cout << "❌ Snapshot 读取 a 失败\n";
    }
    
    if (db.get("b", snap, value)) {
        std::cout << "✅ Snapshot 读取 b = " << value << " (期望: 2)\n";
        if (value != "2") {
            std::cout << "❌ 错误：Snapshot 应该看到旧值 2\n";
        }
    } else {
        std::cout << "❌ Snapshot 读取 b 失败\n";
    }
    
    if (db.get("c", snap, value)) {
        std::cout << "❌ 错误：Snapshot 不应该看到 c（在 snapshot 之后写入）\n";
    } else {
        std::cout << "✅ Snapshot 读取 c 失败（正确，c 在 snapshot 之后写入）\n";
    }
    
    // 5. 使用最新视图读取（应该看到新数据）
    if (db.get("a", value)) {
        std::cout << "✅ 最新视图读取 a = " << value << " (期望: 3)\n";
        if (value != "3") {
            std::cout << "❌ 错误：最新视图应该看到新值 3\n";
        }
    } else {
        std::cout << "❌ 最新视图读取 a 失败\n";
    }
    
    if (db.get("c", value)) {
        std::cout << "✅ 最新视图读取 c = " << value << " (期望: 5)\n";
        if (value != "5") {
            std::cout << "❌ 错误：最新视图应该看到新值 5\n";
        }
    } else {
        std::cout << "❌ 最新视图读取 c 失败\n";
    }
    
    std::cout << "MVCC + Snapshot 测试完成\n";
}

void test_iterator_range_scan() {
    std::cout << "\n=== Test Iterator + Range Scan ===\n";
    
    // 清理之前的文件
    std::filesystem::remove("MANIFEST");
    std::filesystem::remove("iterator_test.log");
    
    // 创建新的KVDB实例
    KVDB db("iterator_test.log");
    
    // 1. 写入一些有序的数据
    std::vector<std::string> keys = {"a1", "a2", "a3", "b1", "b2", "c1", "c2", "d1"};
    for (const auto& key : keys) {
        db.put(key, "value_" + key);
    }
    std::cout << "写入 " << keys.size() << " 个 key\n";
    
    // 2. 创建 Snapshot
    Snapshot snap = db.get_snapshot();
    std::cout << "创建 Snapshot，seq=" << snap.seq << "\n";
    
    // 3. 继续写入新数据
    db.put("a2", "new_value_a2"); // 更新 a2
    db.put("e1", "value_e1");      // 新增 e1
    db.del("b1");                  // 删除 b1
    std::cout << "写入新数据：更新 a2，新增 e1，删除 b1\n";
    
    // 4. 使用 Snapshot 进行 Range Scan
    std::cout << "\n--- Snapshot Range Scan (a1 到 c2) ---\n";
    auto it = db.new_iterator(snap);
    int count = 0;
    for (it->seek("a1"); it->valid() && it->key() <= "c2"; it->next()) {
        std::string value = it->value();
        if (!value.empty()) {
            std::cout << "  " << it->key() << " = " << value << "\n";
            count++;
        }
    }
    std::cout << "找到 " << count << " 个 key\n";
    
    // 5. 使用最新视图进行 Range Scan
    std::cout << "\n--- 最新视图 Range Scan (a1 到 e1) ---\n";
    Snapshot latest = db.get_snapshot();
    auto it2 = db.new_iterator(latest);
    count = 0;
    std::vector<std::string> found_keys;
    for (it2->seek("a1"); it2->valid() && it2->key() <= "e1"; it2->next()) {
        std::string value = it2->value();
        if (!value.empty()) {
            std::cout << "  " << it2->key() << " = " << value << "\n";
            found_keys.push_back(it2->key());
            count++;
        }
    }
    std::cout << "找到 " << count << " 个 key\n";
    
    // 6. 验证结果
    bool all_ordered = true;
    for (size_t i = 1; i < found_keys.size(); i++) {
        if (found_keys[i] < found_keys[i-1]) {
            all_ordered = false;
            break;
        }
    }
    if (all_ordered) {
        std::cout << "✅ 结果按 key 有序\n";
    } else {
        std::cout << "❌ 错误：结果未按 key 有序\n";
    }
    
    // 7. 验证 Snapshot 隔离性
    bool snapshot_correct = true;
    auto it3 = db.new_iterator(snap);
    for (it3->seek("a2"); it3->valid() && it3->key() == "a2"; it3->next()) {
        if (it3->value() != "value_a2") {
            snapshot_correct = false;
            std::cout << "❌ 错误：Snapshot 应该看到旧值 value_a2，实际看到 " << it3->value() << "\n";
            break;
        }
    }
    if (snapshot_correct) {
        std::cout << "✅ Snapshot 隔离性正确（看到旧值）\n";
    }
    
    // 8. 验证删除的 key 不出现
    bool deleted_correct = true;
    auto it4 = db.new_iterator(latest);
    for (it4->seek("b1"); it4->valid() && it4->key() == "b1"; it4->next()) {
        deleted_correct = false;
        std::cout << "❌ 错误：已删除的 b1 不应该出现\n";
        break;
    }
    if (deleted_correct) {
        std::cout << "✅ 已删除的 key 正确过滤\n";
    }
    
    std::cout << "Iterator + Range Scan 测试完成\n";
}

int main(int argc, char* argv[]) {
    // 如果传入参数 "test"，运行测试
    if (argc > 1 && std::string(argv[1]) == "test") {
        test_v12();
        test_mvcc_snapshot();
        test_iterator_range_scan();
        return 0;
    }
    
    // 否则启动 REPL
    std::cout << "Starting KVDB...\n";
    KVDB db("kvdb.log");
    REPL repl(db);
    repl.run();
    
    return 0;
}