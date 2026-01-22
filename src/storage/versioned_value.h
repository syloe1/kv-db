#pragma once
#include <string>
#include <cstdint>

struct VersionedValue {
    uint64_t seq;
    std::string value; // "__TOMBSTONE__" 表示删除
    
    VersionedValue() = default;
    VersionedValue(uint64_t s, const std::string& v) : seq(s), value(v) {}
};
