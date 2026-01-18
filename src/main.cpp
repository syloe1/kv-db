#include <iostream>
#include "db/kv_db.h"

int main() {
    KVDB db("kv.log");

    db.put("name", "Alice");
    db.put("age", "18");

    std::string value;
    if (db.get("name", value)) {
        std::cout << "name = " << value << std::endl;
    }

    db.del("age");
    return 0;
}