#pragma once
#include "snapshot/snapshot.h"
#include <set>
#include <mutex>

class SnapshotManager {
public:
    Snapshot create(uint64_t seq);
    void release(const Snapshot& s);
    uint64_t min_seq() const; // 返回最小活跃 snapshot seq，如果没有则返回 0

private:
    mutable std::mutex mutex_;
    std::set<uint64_t> active_;
};
