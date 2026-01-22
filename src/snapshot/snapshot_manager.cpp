#include "snapshot/snapshot_manager.h"

Snapshot SnapshotManager::create(uint64_t seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_.insert(seq);
    return Snapshot(seq);
}

void SnapshotManager::release(const Snapshot& s) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_.erase(s.seq);
}

uint64_t SnapshotManager::min_seq() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_.empty()) {
        return 0; // 没有活跃 snapshot，可以清理所有旧版本
    }
    return *active_.begin(); // 返回最小的 seq
}
