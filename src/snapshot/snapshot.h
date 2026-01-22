#pragma once
#include <cstdint>

struct Snapshot {
    uint64_t seq;
    
    Snapshot() : seq(0) {}
    explicit Snapshot(uint64_t s) : seq(s) {}
};
