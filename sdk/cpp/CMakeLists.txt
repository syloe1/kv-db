cmake_minimum_required(VERSION 3.16)
project(kvdb-cpp-sdk VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(PkgConfig REQUIRED)
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

# 包含目录
include_directories(include)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# 生成protobuf和gRPC代码
set(PROTO_FILES
    ../../proto/kvdb.proto
)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_FILES})

# 源文件
set(SDK_SOURCES
    src/grpc_client.cpp
    src/http_client.cpp
    src/tcp_client.cpp
    src/client_factory.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

# 创建静态库
add_library(kvdb-client-static STATIC ${SDK_SOURCES})
target_link_libraries(kvdb-client-static
    gRPC::grpc++
    protobuf::libprotobuf
)

# 创建动态库
add_library(kvdb-client-shared SHARED ${SDK_SOURCES})
target_link_libraries(kvdb-client-shared
    gRPC::grpc++
    protobuf::libprotobuf
)

# 设置库属性
set_target_properties(kvdb-client-static PROPERTIES OUTPUT_NAME kvdb-client)
set_target_properties(kvdb-client-shared PROPERTIES OUTPUT_NAME kvdb-client)

# 示例程序
add_executable(basic_example examples/basic_example.cpp)
target_link_libraries(basic_example kvdb-client-static)

# 安装规则
install(TARGETS kvdb-client-static kvdb-client-shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# 包配置
configure_file(kvdb-client.pc.in kvdb-client.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kvdb-client.pc
    DESTINATION lib/pkgconfig
)