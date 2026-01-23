cmake_minimum_required(VERSION 3.10)
project(kvdb VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 检查是否支持网络功能
option(ENABLE_NETWORK "Enable network interfaces (gRPC and WebSocket)" OFF)

# 可执行文件
set(KVDB_SOURCES
    src/main.cpp
    src/db/kv_db.cpp
    src/storage/memtable.cpp
    src/log/wal.cpp
    src/sstable/sstable_writer.cpp
    src/sstable/sstable_reader.cpp
    src/sstable/sstable_meta_util.cpp
    src/sstable/block_index.cpp
    src/compaction/compactor.cpp
    src/compaction/compaction_strategy.cpp
    src/bloom/bloom_filter.cpp
    src/cache/block_cache.cpp
    src/version/version_set.cpp
    src/snapshot/snapshot_manager.cpp
    src/iterator/memtable_iterator.cpp
    src/iterator/sstable_iterator.cpp
    src/iterator/merge_iterator.cpp
    src/iterator/concurrent_iterator.cpp
    src/benchmark/ycsb_benchmark.cpp
    src/cli/repl.cpp
    src/network/tcp_server.cpp
    src/network/http_server.cpp
    # 新增查询引擎
    src/query/query_engine.cpp
    # 新增索引系统
    src/index/secondary_index.cpp
    src/index/composite_index.cpp
    src/index/tokenizer.cpp
    src/index/fulltext_index.cpp
    src/index/inverted_index.cpp
    src/index/index_manager.cpp
    src/index/query_optimizer.cpp
    # 数据类型扩展
    src/storage/data_types.cpp
    src/storage/typed_memtable.cpp
    src/db/typed_kv_db.cpp
    # 序列化支持
    src/storage/serialization_factory.cpp
    src/storage/binary_serializer.cpp
    src/storage/json_serializer.cpp
    src/storage/messagepack_serializer.cpp
    src/storage/protobuf_serializer.cpp
)

# 网络功能源文件
if(ENABLE_NETWORK)
    # 查找 pkg-config
    find_package(PkgConfig QUIET)
    
    if(PKG_CONFIG_FOUND)
        # 查找 gRPC 和相关依赖
        pkg_check_modules(GRPC QUIET grpc++)
        pkg_check_modules(PROTOBUF QUIET protobuf)
        
        # 检查 WebSocket 依赖
        find_path(WEBSOCKETPP_INCLUDE_DIR websocketpp/config/asio_no_tls.hpp)
        find_path(JSON_INCLUDE_DIR jsoncpp/json/json.h)
        
        if(GRPC_FOUND AND PROTOBUF_FOUND AND WEBSOCKETPP_INCLUDE_DIR AND JSON_INCLUDE_DIR)
            message(STATUS "Network dependencies found, enabling network support")
            
            # 生成 protobuf 文件
            find_program(PROTOC protoc)
            find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
            
            if(PROTOC AND GRPC_CPP_PLUGIN)
                execute_process(
                    COMMAND ${PROTOC} --cpp_out=src/network --grpc_out=src/network --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN} proto/kvdb.proto
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    RESULT_VARIABLE PROTOC_RESULT
                )
                
                if(PROTOC_RESULT EQUAL 0)
                    message(STATUS "Generated protobuf files")
                    list(APPEND KVDB_SOURCES
                        src/network/kvdb.pb.cc
                        src/network/kvdb.grpc.pb.cc
                        src/network/grpc_server.cpp
                        src/network/websocket_server.cpp
                        src/network/network_server.cpp
                    )
                    set(NETWORK_ENABLED TRUE)
                else()
                    message(WARNING "Failed to generate protobuf files, disabling network support")
                    set(NETWORK_ENABLED FALSE)
                endif()
            else()
                message(WARNING "protoc or grpc_cpp_plugin not found, disabling network support")
                set(NETWORK_ENABLED FALSE)
            endif()
        else()
            message(WARNING "Network dependencies not found, disabling network support")
            message(STATUS "Missing dependencies:")
            if(NOT GRPC_FOUND)
                message(STATUS "  - gRPC library")
            endif()
            if(NOT PROTOBUF_FOUND)
                message(STATUS "  - Protocol Buffers library")
            endif()
            if(NOT WEBSOCKETPP_INCLUDE_DIR)
                message(STATUS "  - WebSocket++ headers")
            endif()
            if(NOT JSON_INCLUDE_DIR)
                message(STATUS "  - JsonCpp headers")
            endif()
            message(STATUS "To enable network support, install: sudo apt-get install pkg-config libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc libwebsocketpp-dev libjsoncpp-dev")
            set(NETWORK_ENABLED FALSE)
        endif()
    else()
        message(WARNING "pkg-config not found, disabling network support")
        message(STATUS "To enable network support, install: sudo apt-get install pkg-config libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc libwebsocketpp-dev libjsoncpp-dev")
        set(NETWORK_ENABLED FALSE)
    endif()
else()
    set(NETWORK_ENABLED FALSE)
endif()

add_executable(kvdb ${KVDB_SOURCES})

# 查找 readline 库
find_path(READLINE_INCLUDE_DIR readline/readline.h)
find_library(READLINE_LIBRARY readline)

# 查找序列化相关库
find_library(ZLIB_LIBRARY z)
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
find_path(MSGPACK_INCLUDE_DIR msgpack.hpp)

# 检查序列化依赖
set(SERIALIZATION_LIBS "")
set(SERIALIZATION_INCLUDES "")

if(ZLIB_LIBRARY)
    list(APPEND SERIALIZATION_LIBS ${ZLIB_LIBRARY})
    message(STATUS "Found zlib: ${ZLIB_LIBRARY}")
else()
    message(WARNING "zlib not found, compression features will be disabled")
    message(STATUS "To enable compression, install: sudo apt-get install zlib1g-dev")
endif()

if(NLOHMANN_JSON_INCLUDE_DIR)
    list(APPEND SERIALIZATION_INCLUDES ${NLOHMANN_JSON_INCLUDE_DIR})
    target_compile_definitions(kvdb PRIVATE HAVE_NLOHMANN_JSON)
    message(STATUS "Found nlohmann/json: ${NLOHMANN_JSON_INCLUDE_DIR}")
else()
    message(STATUS "nlohmann/json not found, JSON serialization will use fallback implementation")
    message(STATUS "To enable full JSON support, install: sudo apt-get install nlohmann-json3-dev")
endif()

if(MSGPACK_INCLUDE_DIR)
    list(APPEND SERIALIZATION_INCLUDES ${MSGPACK_INCLUDE_DIR})
    target_compile_definitions(kvdb PRIVATE HAVE_MSGPACK)
    message(STATUS "Found MessagePack: ${MSGPACK_INCLUDE_DIR}")
else()
    message(STATUS "MessagePack not found, MessagePack serialization will be disabled")
    message(STATUS "To enable MessagePack support, install: sudo apt-get install libmsgpack-dev")
endif()

if(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
    target_include_directories(kvdb PRIVATE ${READLINE_INCLUDE_DIR})
    target_link_libraries(kvdb ${READLINE_LIBRARY})
    target_compile_definitions(kvdb PRIVATE HAVE_READLINE)
    message(STATUS "Found readline: ${READLINE_LIBRARY}")
else()
    # 尝试查找 libreadline-dev 包
    message(STATUS "readline not found")
    message(STATUS "To enable command history and completion, install: sudo apt-get install libreadline-dev")
    message(STATUS "Continuing without readline support...")
endif()

# 链接序列化相关库
if(SERIALIZATION_LIBS)
    target_link_libraries(kvdb ${SERIALIZATION_LIBS})
endif()

if(SERIALIZATION_INCLUDES)
    target_include_directories(kvdb PRIVATE ${SERIALIZATION_INCLUDES})
endif()

# 网络功能链接库
if(NETWORK_ENABLED)
    target_compile_definitions(kvdb PRIVATE ENABLE_NETWORK)
    target_include_directories(kvdb PRIVATE ${WEBSOCKETPP_INCLUDE_DIR} ${JSON_INCLUDE_DIR})
    target_link_libraries(kvdb ${GRPC_LIBRARIES} ${PROTOBUF_LIBRARIES} jsoncpp pthread)
    message(STATUS "Network support enabled")
else()
    message(STATUS "Network support disabled")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_link_libraries(kvdb stdc++fs)
endif()

# 头文件目录
target_include_directories(kvdb PRIVATE src)
