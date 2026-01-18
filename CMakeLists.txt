cmake_minimum_required(VERSION 3.10)
project(kv)

set(CMAKE_CXX_STANDARD 17)

include_directories(src)

add_executable(kv
    src/main.cpp
    src/db/kv_db.cpp
    src/log/wal.cpp
    src/storage/memtable.cpp
)
