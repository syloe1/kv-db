#include <iostream>
#include "db/kv_db.h"

int main() {
    std::cout << "=== 步骤1：创建KVDB并写入数据 ===" << std::endl;
    KVDB db("kvv.log");

    // 写入数据到WAL
    db.put("name", "Alice");
    std::cout << "✅ PUT name Alice" << std::endl;

    db.put("age", "18");
    std::cout << "✅ PUT age 18" << std::endl;

    db.put("city", "Beijing");
    std::cout << "✅ PUT city Beijing" << std::endl;

    db.del("age");
    std::cout << "✅ DEL age" << std::endl;

    std::cout << "\n=== 步骤2：程序将退出（模拟崩溃）===" << std::endl;
    std::cout << "现在数据已保存到 kvv.log 文件" << std::endl;
    std::cout << "下一步运行 ./kv 来验证WAL重放" << std::endl;

    return 0;
}
