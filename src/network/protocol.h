#pragma once

#include <cstdint>
#include <string>

namespace kvdb {
namespace network {

// 操作码定义
enum class Opcode : uint8_t {
    PUT = 0,
    GET = 1,
    DEL = 2,
    SCAN = 3,
    PREFIX_SCAN = 4,
    STATS = 5
};

// 状态码定义
enum class Status : uint8_t {
    SUCCESS = 0,
    KEY_NOT_FOUND = 1,
    INVALID_REQUEST = 2,
    INTERNAL_ERROR = 3
};

// 请求头
struct RequestHeader {
    uint32_t magic = 0x4B564442; // "KVDB"
    uint8_t opcode;              // 操作码
    uint32_t key_length;         // 键长度
    uint32_t value_length;       // 值长度
};

// 响应头
struct ResponseHeader {
    uint32_t magic = 0x4B564442; // "KVDB"
    uint8_t status;              // 状态码
    uint32_t value_length;       // 返回值长度
};

// 协议常量
constexpr uint32_t PROTOCOL_MAGIC = 0x4B564442;
constexpr size_t MAX_KEY_SIZE = 1024;
constexpr size_t MAX_VALUE_SIZE = 1024 * 1024; // 1MB

} // namespace network
} // namespace kvdb