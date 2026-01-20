cmake_minimum_required(VERSION 3.10)
project(kv)

set(CMAKE_CXX_STANDARD 17)

# 添加头文件搜索路径
include_directories(src)

# 主程序
add_executable(kv
    src/main.cpp
    src/db/kv_db.cpp
    src/log/wal.cpp
    src/storage/memtable.cpp
)

# 写入数据测试程序
add_executable(write_data
    src/write_data.cpp
    src/db/kv_db.cpp
    src/log/wal.cpp
    src/storage/memtable.cpp
)

