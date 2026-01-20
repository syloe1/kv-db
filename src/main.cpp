#include <iostream>
#include "db/kv_db.h"

int main() {
    // 1. 创建 KVDB 对象 → 此时会自动调用 WAL::replay 解析 kvv.log
    //    日志中的 PUT/DEL 操作会被重放到 MemTable 中
    KVDB db("kvv.log");

    std::cout << "===== 验证 WAL 重放结果 =====" << std::endl;

    // 2. 验证 PUT name Alice 是否被重放
    std::string name_val;
    if (db.get("name", name_val)) {
        std::cout << "✅ name 重放成功：name = " << name_val << std::endl;
    } else {
        std::cout << "❌ name 重放失败：未找到该键" << std::endl;
    }

    // 3. 验证 PUT age 18 → DEL age 是否被重放（最终 age 应该不存在）
    std::string age_val;
    if (db.get("age", age_val)) {
        std::cout << "❌ age 重放失败：DEL 操作未生效，age = " << age_val << std::endl;
    } else {
        std::cout << "✅ age 重放成功：DEL 操作生效，age 不存在" << std::endl;
    }

    return 0;
}