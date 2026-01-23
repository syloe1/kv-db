#include "concurrent_memtable.h"
#include <iostream>
#include <algorithm>

const std::string ConcurrentMemTable::TOMBSTONE = "__TOMBSTONE__";

// ConcurrentMemTable 实现
ConcurrentMemTable::ConcurrentMemTable() {
    std::cout << "[ConcurrentMemTable] 初始化无锁MemTable\n";
}

ConcurrentMemTable::~ConcurrentMemTable() = default;

void ConcurrentMemTable::put(const std::string& key, const std::string& value, uint64_t seq) {
    stats_.total_puts.fetch_add(1);
    
    // 使用TBB并发哈希表的访问器
    ConcurrentMap::accessor accessor;
    table_.insert(accessor, key);
    
    // 向并发向量添加新版本
    accessor->second.push_back({seq, value});
    
    // 更新大小统计
    size_bytes_.fetch_add(key.size() + value.size() + sizeof(uint64_t));
}

bool ConcurrentMemTable::get(const std::string& key, uint64_t snapshot_seq, std::string& value) const {
    stats_.total_gets.fetch_add(1);
    
    ConcurrentMap::const_accessor accessor;
    if (!table_.find(accessor, key)) {
        return false;
    }
    
    const auto& versions = accessor->second;
    
    // 从最新版本开始查找
    for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
        if (it->seq <= snapshot_seq) {
            if (it->value == TOMBSTONE) {
                return false;
            }
            value = it->value;
            return true;
        }
    }
    
    return false;
}

void ConcurrentMemTable::del(const std::string& key, uint64_t seq) {
    stats_.total_deletes.fetch_add(1);
    put(key, TOMBSTONE, seq);
}

void ConcurrentMemTable::batch_put(const std::vector<std::tuple<std::string, std::string, uint64_t>>& operations) {
    stats_.batch_operations.fetch_add(1);
    
    for (const auto& [key, value, seq] : operations) {
        put(key, value, seq);
    }
}

size_t ConcurrentMemTable::size() const {
    return size_bytes_.load();
}

void ConcurrentMemTable::clear() {
    table_.clear();
    size_bytes_.store(0);
}

std::map<std::string, std::vector<VersionedValue>> ConcurrentMemTable::get_all_versions() const {
    std::map<std::string, std::vector<VersionedValue>> result;
    
    for (ConcurrentMap::const_iterator it = table_.begin(); it != table_.end(); ++it) {
        const std::string& key = it->first;
        const auto& versions = it->second;
        
        std::vector<VersionedValue> version_list;
        for (const auto& version : versions) {
            version_list.push_back(version);
        }
        
        result[key] = std::move(version_list);
    }
    
    return result;
}

void ConcurrentMemTable::print_stats() const {
    std::cout << "\n=== 并发MemTable统计 ===\n";
    std::cout << "总写入次数: " << stats_.total_puts.load() << "\n";
    std::cout << "总读取次数: " << stats_.total_gets.load() << "\n";
    std::cout << "总删除次数: " << stats_.total_deletes.load() << "\n";
    std::cout << "锁竞争次数: " << stats_.lock_contentions.load() << "\n";
    std::cout << "批量操作次数: " << stats_.batch_operations.load() << "\n";
    std::cout << "内存使用: " << size_bytes_.load() << " bytes\n";
    std::cout << "========================\n\n";
}

// ReadWriteSeparatedMemTable 实现
ReadWriteSeparatedMemTable::ReadWriteSeparatedMemTable() 
    : active_table_(std::make_unique<ConcurrentMemTable>()),
      immutable_table_(nullptr),
      write_worker_(&ReadWriteSeparatedMemTable::write_worker_thread, this) {
    std::cout << "[ReadWriteSeparatedMemTable] 初始化读写分离MemTable\n";
}

ReadWriteSeparatedMemTable::~ReadWriteSeparatedMemTable() {
    stop_worker_.store(true);
    write_buffer_.buffer_cv.notify_all();
    if (write_worker_.joinable()) {
        write_worker_.join();
    }
}

bool ReadWriteSeparatedMemTable::get(const std::string& key, uint64_t snapshot_seq, std::string& value) const {
    // 读操作使用共享锁，允许多个读者并发
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    active_readers_.fetch_add(1);
    
    // 先查询活跃表
    bool found = active_table_->get(key, snapshot_seq, value);
    
    // 如果没找到且存在不可变表，查询不可变表
    if (!found && immutable_table_) {
        found = immutable_table_->get(key, snapshot_seq, value);
    }
    
    active_readers_.fetch_sub(1);
    return found;
}

void ReadWriteSeparatedMemTable::put(const std::string& key, const std::string& value, uint64_t seq) {
    // 写操作添加到缓冲区，由后台线程批量处理
    {
        std::lock_guard<std::mutex> lock(write_buffer_.buffer_mutex);
        write_buffer_.pending_writes.emplace_back(key, value, seq);
        
        // 如果缓冲区达到一定大小，触发刷新
        if (write_buffer_.pending_writes.size() >= 100) {
            write_buffer_.flush_requested.store(true);
            write_buffer_.buffer_cv.notify_one();
        }
    }
}

void ReadWriteSeparatedMemTable::del(const std::string& key, uint64_t seq) {
    put(key, ConcurrentMemTable::TOMBSTONE, seq);
}

void ReadWriteSeparatedMemTable::batch_write(const std::vector<std::tuple<std::string, std::string, uint64_t>>& operations) {
    {
        std::lock_guard<std::mutex> lock(write_buffer_.buffer_mutex);
        for (const auto& op : operations) {
            write_buffer_.pending_writes.push_back(op);
        }
        write_buffer_.flush_requested.store(true);
        write_buffer_.buffer_cv.notify_one();
    }
}

void ReadWriteSeparatedMemTable::write_worker_thread() {
    while (!stop_worker_.load()) {
        std::unique_lock<std::mutex> lock(write_buffer_.buffer_mutex);
        
        // 等待写入请求或停止信号
        write_buffer_.buffer_cv.wait(lock, [this] {
            return write_buffer_.flush_requested.load() || stop_worker_.load();
        });
        
        if (stop_worker_.load()) break;
        
        if (write_buffer_.flush_requested.load()) {
            flush_write_buffer();
            write_buffer_.flush_requested.store(false);
        }
    }
}

void ReadWriteSeparatedMemTable::flush_write_buffer() {
    if (write_buffer_.pending_writes.empty()) return;
    
    // 获取独占锁进行写入
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    // 等待所有读者完成
    while (active_readers_.load() > 0) {
        std::this_thread::yield();
    }
    
    // 批量写入到活跃表
    active_table_->batch_put(write_buffer_.pending_writes);
    write_buffer_.pending_writes.clear();
}

size_t ReadWriteSeparatedMemTable::size() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    size_t total = active_table_->size();
    if (immutable_table_) {
        total += immutable_table_->size();
    }
    return total;
}

void ReadWriteSeparatedMemTable::clear() {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    active_table_->clear();
    immutable_table_.reset();
}

std::map<std::string, std::vector<VersionedValue>> ReadWriteSeparatedMemTable::get_all_versions() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto result = active_table_->get_all_versions();
    
    if (immutable_table_) {
        auto immutable_data = immutable_table_->get_all_versions();
        // 合并两个表的数据
        for (auto& [key, versions] : immutable_data) {
            auto& target_versions = result[key];
            target_versions.insert(target_versions.end(), versions.begin(), versions.end());
        }
    }
    
    return result;
}