#include "db/kv_db.h"
#include "sstable/sstable_writer.h"
#include "sstable/sstable_reader.h"
#include "sstable/sstable_meta_util.h"
#include "compaction/compactor.h"
#include "compaction/compaction_strategy.h"
#include "iterator/memtable_iterator.h"
#include "iterator/sstable_iterator.h"
#include "iterator/merge_iterator.h"
#include "iterator/concurrent_iterator.h"
#include "index/index_manager.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>

static const std::string TOMBSTONE = "__TOMBSTONE__";

KVDB::KVDB(const std::string& wal_file)
    : wal_(wal_file), file_id_(0), version_set_(MAX_LEVEL) {
    // 创建数据目录
    std::filesystem::create_directories("data");
    
    // 初始化多级缓存管理器
    cache_manager_ = std::make_unique<CacheManager>(
        CacheManager::CacheType::MULTI_LEVEL_CACHE, 1024, 8192);
    
    // 初始化多级结构
    levels_.resize(MAX_LEVEL);
    
    // 初始化默认压缩策略
    std::vector<size_t> level_limits = {
        4ULL * 1024 * 1024,    // L0: 4MB
        40ULL * 1024 * 1024,   // L1: 40MB  
        400ULL * 1024 * 1024,  // L2: 400MB
        4000ULL * 1024 * 1024  // L3: 4GB
    };
    compaction_strategy_ = CompactionStrategyFactory::create_strategy(
        CompactionStrategyType::LEVELED, level_limits);
    
    std::cout << "[KVDB] 初始化，WAL文件: " << wal_file << std::endl;
    
    // 启动顺序：1. 读 Manifest 2. 重建 Version 3. 打开 WAL 4. 重放 WAL
    version_set_.recover();
    
    // 从 VersionSet 恢复数据到 levels_
    const Version& version = version_set_.current();
    for (int level = 0; level < MAX_LEVEL; level++) {
        std::lock_guard<std::mutex> lock(levels_[level].mutex);
        levels_[level].sstables = version.levels[level];
        if (!levels_[level].sstables.empty()) {
            std::cout << "[KVDB] 从 MANIFEST 恢复 L" << level 
                      << "，共 " << levels_[level].sstables.size() << " 个 SSTable\n";
        }
    }
    
    // 启动时 WAL 重放
    // 注意：WAL 重放时使用当前序列号，确保不会覆盖新数据
    wal_.replay(
        [this](const std::string& key, const std::string& value) {
            std::cout << "[KVDB重放] 恢复 key: " << key << " = " << value << std::endl;
            uint64_t seq = next_seq();
            memtable_.put(key, value, seq);
        },
        [this](const std::string& key) {
            std::cout << "[KVDB重放] 删除 key: " << key << std::endl;
            uint64_t seq = next_seq();
            memtable_.del(key, seq);
        }
    );
    
    // 启动后台线程
    bg_flush_thread_ = std::thread(&KVDB::flush_worker, this);
    bg_compact_thread_ = std::thread(&KVDB::compact_worker, this);
    
    // 初始化索引管理器
    index_manager_ = std::make_unique<IndexManager>(*this);
}

KVDB::~KVDB() {
    stop_.store(true);
    flush_cv_.notify_one();
    compact_cv_.notify_one();
    if (bg_flush_thread_.joinable()) {
        bg_flush_thread_.join();
    }
    if (bg_compact_thread_.joinable()) {
        bg_compact_thread_.join();
    }
}

uint64_t KVDB::next_seq() {
    return seq_.fetch_add(1, std::memory_order_relaxed);
}

Snapshot KVDB::get_snapshot() {
    // Snapshot 应该看到创建时刻及之前的所有版本
    // fetch_add 返回的是旧值，所以当前 seq_ 是下一个 put 会得到的值
    // 我们需要返回 seq_ - 1，这样 snapshot 能看到所有已完成的 put
    uint64_t current_seq = seq_.load(std::memory_order_relaxed);
    // 如果 seq_ > 0，返回 seq_ - 1；否则返回 0
    uint64_t snapshot_seq = (current_seq > 0) ? current_seq - 1 : 0;
    return snapshot_manager_.create(snapshot_seq);
}

void KVDB::release_snapshot(const Snapshot& snapshot) {
    snapshot_manager_.release(snapshot);
}

bool KVDB::put(const std::string& key, const std::string& value) {
    begin_write_operation();
    
    // 获取旧值用于索引更新
    std::string old_value;
    bool had_old_value = get(key, old_value);
    
    uint64_t seq = next_seq();
    wal_.log_put(key, value);
    memtable_.put(key, value, seq);
    
    // 更新索引
    if (index_manager_) {
        if (had_old_value) {
            index_manager_->update_indexes(key, old_value, value);
        } else {
            index_manager_->add_to_indexes(key, value);
        }
    }
    
    if (memtable_.size() >= MEMTABLE_LIMIT) {
        request_flush();
    }
    
    end_write_operation();
    return true;
}

bool KVDB::del(const std::string& key) {
    begin_write_operation();
    
    // 获取旧值用于索引更新
    std::string old_value;
    bool had_value = get(key, old_value);
    
    uint64_t seq = next_seq();
    wal_.log_del(key);
    memtable_.del(key, seq);
    
    // 从索引中移除
    if (index_manager_ && had_value) {
        index_manager_->remove_from_indexes(key, old_value);
    }
    
    if (memtable_.size() >= MEMTABLE_LIMIT) {
        request_flush();
    }
    
    end_write_operation();
    return true;
}

std::unique_ptr<Iterator> KVDB::new_iterator(const Snapshot& snapshot) {
    std::vector<std::unique_ptr<Iterator>> iters;

    // 1. 添加 MemTable Iterator
    iters.push_back(
        std::make_unique<MemTableIterator>(memtable_, snapshot.seq));

    // 2. 添加所有 SSTable Iterator（从 L0 到 LMAX，从新到旧）
    const Version& version = version_set_.current();
    for (int level = 0; level < MAX_LEVEL; level++) {
        std::lock_guard<std::mutex> lock(levels_[level].mutex);
        // L0: 从新到旧（rbegin）
        // L1+: 从旧到新（begin）
        if (level == 0) {
            for (auto it = levels_[level].sstables.rbegin(); 
                 it != levels_[level].sstables.rend(); ++it) {
                iters.push_back(
                    std::make_unique<SSTableIterator>(*it, snapshot.seq));
            }
        } else {
            for (const auto& meta : levels_[level].sstables) {
                iters.push_back(
                    std::make_unique<SSTableIterator>(meta, snapshot.seq));
            }
        }
    }

    return std::make_unique<MergeIterator>(std::move(iters));
}

std::unique_ptr<Iterator> KVDB::new_prefix_iterator(const Snapshot& snapshot, const std::string& prefix) {
    std::vector<std::unique_ptr<Iterator>> iters;

    // 1. 添加 MemTable Iterator with prefix
    auto mem_iter = std::make_unique<MemTableIterator>(memtable_, snapshot.seq);
    mem_iter->seek_with_prefix(prefix);
    if (mem_iter->valid()) {
        iters.push_back(std::move(mem_iter));
    }

    // 2. 添加所有 SSTable Iterator with prefix（从 L0 到 LMAX，从新到旧）
    const Version& version = version_set_.current();
    for (int level = 0; level < MAX_LEVEL; level++) {
        std::lock_guard<std::mutex> lock(levels_[level].mutex);
        // L0: 从新到旧（rbegin）
        // L1+: 从旧到新（begin）
        if (level == 0) {
            for (auto it = levels_[level].sstables.rbegin(); 
                 it != levels_[level].sstables.rend(); ++it) {
                auto sstable_iter = std::make_unique<SSTableIterator>(*it, snapshot.seq);
                sstable_iter->seek_with_prefix(prefix);
                if (sstable_iter->valid()) {
                    iters.push_back(std::move(sstable_iter));
                }
            }
        } else {
            for (const auto& meta : levels_[level].sstables) {
                auto sstable_iter = std::make_unique<SSTableIterator>(meta, snapshot.seq);
                sstable_iter->seek_with_prefix(prefix);
                if (sstable_iter->valid()) {
                    iters.push_back(std::move(sstable_iter));
                }
            }
        }
    }

    if (iters.empty()) {
        // 返回一个空的迭代器
        return std::make_unique<MergeIterator>(std::move(iters));
    }

    auto merge_iter = std::make_unique<MergeIterator>(std::move(iters));
    merge_iter->seek_with_prefix(prefix);
    return merge_iter;
}

std::shared_ptr<ConcurrentIterator> KVDB::new_concurrent_iterator(const Snapshot& snapshot) {
    std::shared_lock<std::shared_mutex> lock(db_rw_mutex_);
    
    auto inner_iter = new_iterator(snapshot);
    auto concurrent_iter = std::make_shared<ConcurrentIterator>(std::move(inner_iter));
    
    IteratorManager::instance().register_iterator(concurrent_iter);
    return concurrent_iter;
}

std::shared_ptr<ConcurrentIterator> KVDB::new_concurrent_prefix_iterator(const Snapshot& snapshot, const std::string& prefix) {
    std::shared_lock<std::shared_mutex> lock(db_rw_mutex_);
    
    auto inner_iter = new_prefix_iterator(snapshot, prefix);
    auto concurrent_iter = std::make_shared<ConcurrentIterator>(std::move(inner_iter));
    
    IteratorManager::instance().register_iterator(concurrent_iter);
    return concurrent_iter;
}

void KVDB::begin_write_operation() {
    // 获取写锁，阻塞新的读操作
    db_rw_mutex_.lock();
    
    // 使所有现有迭代器失效
    IteratorManager::instance().invalidate_all_iterators();
    
    // 等待所有正在进行的读操作完成
    IteratorManager::instance().wait_for_iterators();
}

void KVDB::end_write_operation() {
    db_rw_mutex_.unlock();
}

void KVDB::set_compaction_strategy(CompactionStrategyType type) {
    std::lock_guard<std::mutex> lock(compaction_strategy_mutex_);
    
    std::vector<size_t> level_limits = {
        4ULL * 1024 * 1024,    // L0: 4MB
        40ULL * 1024 * 1024,   // L1: 40MB  
        400ULL * 1024 * 1024,  // L2: 400MB
        4000ULL * 1024 * 1024  // L3: 4GB
    };
    
    compaction_strategy_ = CompactionStrategyFactory::create_strategy(type, level_limits);
    
    std::cout << "[Compaction] 切换到策略: ";
    switch (type) {
        case CompactionStrategyType::LEVELED:
            std::cout << "Leveled Compaction\n";
            break;
        case CompactionStrategyType::TIERED:
            std::cout << "Tiered Compaction\n";
            break;
        case CompactionStrategyType::SIZE_TIERED:
            std::cout << "Size-Tiered Compaction\n";
            break;
        case CompactionStrategyType::TIME_WINDOW:
            std::cout << "Time Window Compaction\n";
            break;
    }
}

CompactionStrategyType KVDB::get_compaction_strategy() const {
    std::lock_guard<std::mutex> lock(compaction_strategy_mutex_);
    return compaction_strategy_->get_type();
}

const CompactionStats& KVDB::get_compaction_stats() const {
    std::lock_guard<std::mutex> lock(compaction_strategy_mutex_);
    return compaction_strategy_->get_stats();
}

size_t KVDB::get_memtable_size() const {
    return memtable_.size();
}

size_t KVDB::get_wal_size() const {
    std::filesystem::path wal_path(wal_.get_filename());
    if (std::filesystem::exists(wal_path)) {
        return std::filesystem::file_size(wal_path);
    }
    return 0;
}

void KVDB::print_lsm_structure() const {
    const Version& version = version_set_.current();
    for (int level = 0; level < MAX_LEVEL; level++) {
        std::lock_guard<std::mutex> lock(levels_[level].mutex);
        if (levels_[level].sstables.empty()) continue;
        
        std::cout << "L" << level << ":\n";
        for (const auto& meta : levels_[level].sstables) {
            std::filesystem::path p(meta.filename);
            std::string filename = p.filename().string();
            std::cout << "  " << filename << " [" << meta.min_key 
                      << ", " << meta.max_key << "]\n";
        }
    }
}

void KVDB::request_flush() {
    flush_requested_.store(true);
    flush_cv_.notify_one();
}

void KVDB::request_compaction() {
    compaction_requested_.store(true);
    flush_cv_.notify_one();
}

bool KVDB::need_compaction() const {
    std::lock_guard<std::mutex> strategy_lock(compaction_strategy_mutex_);
    
    // 构建当前层级状态
    std::vector<std::vector<SSTableMeta>> current_levels(MAX_LEVEL);
    for (int level = 0; level < MAX_LEVEL; level++) {
        std::lock_guard<std::mutex> lock(levels_[level].mutex);
        current_levels[level] = levels_[level].sstables;
    }
    
    return compaction_strategy_->needs_compaction(current_levels);
}

void KVDB::flush_worker() {
    while (true) {
        std::unique_lock<std::mutex> lock(flush_mutex_);
        flush_cv_.wait(lock, [&] {
            return flush_requested_.load() || compaction_requested_.load() || stop_;
        });

        if (stop_) break;

        if (flush_requested_.load()) {
            flush_requested_.store(false);
            lock.unlock();
            
            flush();  // ⚠️ 真正的 IO
            
            if (need_compaction()) {
                compact();
            }
        } else if (compaction_requested_.load()) {
            compaction_requested_.store(false);
            lock.unlock();
            
            compact();
        }
    }
}

void KVDB::flush() {
    auto all_versions = memtable_.get_all_versions();
    if (all_versions.empty()) {
        std::cout << "MemTable 为空，无需刷盘\n";
        return;
    }
    
    // 生成 SSTable 文件名
    std::string filename = "data/sstable_" + std::to_string(file_id_++) + ".dat";
    
    std::cout << "刷盘到: " << filename << std::endl;
    
    // 写入 SSTable（多版本格式）
    SSTableWriter::write(filename, all_versions);
    
    // 获取 SSTable 元数据并添加到 L0
    SSTableMeta meta = SSTableMetaUtil::get_meta_from_file(filename);
    {
        std::lock_guard<std::mutex> lock(levels_[0].mutex);
        levels_[0].sstables.push_back(meta);
        std::cout << "添加到 L0，当前 L0 SSTable 数量: " << levels_[0].sstables.size() << std::endl;
        
        // 先写 Manifest，再改内存 Version
        version_set_.persist_add(meta, 0);
        version_set_.add_file(0, meta);
    }

    // 清空 MemTable
    memtable_.clear();
    
    // 清空 WAL 文件
    std::ofstream ofs(wal_.get_filename(), std::ios::trunc);
    
    std::cout << "刷盘完成，MemTable 已清空\n";
}

bool KVDB::get(const std::string& key, std::string& value) {
    // 使用当前最新序列号作为 snapshot
    uint64_t current_seq = seq_.load(std::memory_order_relaxed);
    return get(key, Snapshot(current_seq), value);
}

bool KVDB::get(const std::string& key, const Snapshot& snapshot, std::string& value) {
    uint64_t snapshot_seq = snapshot.seq;
    
    // 创建缓存引用
    BlockCache& cache = cache_manager_->get_block_cache();
    
    // 1. 先检查MemTable
    if (memtable_.get(key, snapshot_seq, value)) {
        return true;
    }
    
    // 2. 检查L0（所有SSTable，从最新到最旧）
    {
        std::lock_guard<std::mutex> lock(levels_[0].mutex);
        for (auto it = levels_[0].sstables.rbegin(); it != levels_[0].sstables.rend(); it++) {
            if (it->contains_key(key)) {
                auto result = SSTableReader::get(it->filename, key, snapshot_seq, cache);
                if (result.has_value()) {
                    value = result.value();
                    return true;
                }
            }
        }
    }
    
    // 3. 检查L1+层级（使用二分查找优化）
    for (int level = 1; level < MAX_LEVEL; level++) {
        std::lock_guard<std::mutex> lock(levels_[level].mutex);
        
        for (const auto& sstable : levels_[level].sstables) {
            if (sstable.contains_key(key)) {
                auto result = SSTableReader::get(sstable.filename, key, snapshot_seq, cache);
                if (result.has_value()) {
                    value = result.value();
                    return true;
                }
            }
        }
    }
    
    return false;
}

void KVDB::compact_worker() {
    while (!stop_.load()) {
        std::unique_lock<std::mutex> lock(compact_mutex_);
        compact_cv_.wait_for(lock, std::chrono::seconds(1), [this] {
            return stop_.load() || need_compaction();
        });
        
        if (stop_.load()) {
            break;
        }
        
        if (need_compaction()) {
            compact();
        }
    }
}

void KVDB::compact() {
    std::unique_ptr<CompactionTask> task;
    
    {
        std::lock_guard<std::mutex> strategy_lock(compaction_strategy_mutex_);
        
        // 构建当前层级状态
        std::vector<std::vector<SSTableMeta>> current_levels(MAX_LEVEL);
        for (int level = 0; level < MAX_LEVEL; level++) {
            std::lock_guard<std::mutex> lock(levels_[level].mutex);
            current_levels[level] = levels_[level].sstables;
        }
        
        // 选择压缩任务
        task = compaction_strategy_->pick_compaction(current_levels);
    }
    
    if (task) {
        std::cout << "[Compaction] 执行压缩任务: L" << task->source_level 
                  << " -> L" << task->target_level 
                  << ", 文件数: " << task->input_files.size() << std::endl;
        execute_compaction_task(std::move(task));
    }
}

void KVDB::compact_level(int level) {
    if (level >= MAX_LEVEL - 1) {
        std::cout << "[Compaction] L" << level << " 是最高层级，无法继续合并\n";
        return;
    }
    
    std::vector<SSTableMeta> input_sstables;
    {
        std::lock_guard<std::mutex> lock(levels_[level].mutex);
        if (levels_[level].sstables.empty()) {
            std::cout << "[Compaction] L" << level << " 为空，跳过\n";
            return;
        }
        
        std::cout << "[Compaction] 开始 L" << level << " → L" << level + 1 << " 合并\n";
        std::cout << "[Compaction] L" << level << " 有 " << levels_[level].sstables.size() << " 个 SSTable\n";
        
        // L0: 选择所有SSTable
        // L1+: 选择一个SSTable（简化实现，选择第一个）
        if (level == 0) {
            input_sstables = levels_[level].sstables;
        } else {
            input_sstables.push_back(levels_[level].sstables[0]);
        }
    }
    
    // 获取下一层重叠的SSTable
    std::vector<SSTableMeta> next_level_sstables;
    for (const auto& input : input_sstables) {
        auto overlapping = get_overlapping_sstables(level + 1, input);
        next_level_sstables.insert(next_level_sstables.end(), 
                                 overlapping.begin(), overlapping.end());
    }
    
    // 合并所有需要合并的文件
    std::vector<std::string> all_files;
    for (const auto& meta : input_sstables) {
        all_files.push_back(meta.filename);
    }
    for (const auto& meta : next_level_sstables) {
        all_files.push_back(meta.filename);
    }
    
    if (all_files.empty()) {
        std::cout << "[Compaction] 没有文件需要合并\n";
        return;
    }
    
    // 执行合并（传递 min_snapshot_seq 以保留活跃版本）
    uint64_t min_snapshot_seq = snapshot_manager_.min_seq();
    std::string new_filename = "data/sstable_compacted_L" + 
                              std::to_string(level + 1) + "_" + 
                              std::to_string(file_id_++) + ".dat";
    
    std::string new_table = Compactor::compact(all_files, "data", new_filename, min_snapshot_seq);
    
    // 更新元数据
    SSTableMeta new_meta = SSTableMetaUtil::get_meta_from_file(new_table);
    
    // 删除旧文件，添加新文件到下一层
    // 先写 Manifest，再改内存 Version
    for (const auto& old_file : input_sstables) {
        version_set_.persist_del(old_file.filename, level);
    }
    for (const auto& old_file : next_level_sstables) {
        version_set_.persist_del(old_file.filename, level + 1);
    }
    version_set_.persist_add(new_meta, level + 1);
    
    update_level_metadata(level, input_sstables);
    {
        std::lock_guard<std::mutex> lock(levels_[level + 1].mutex);
        levels_[level + 1].sstables.push_back(new_meta);
        std::cout << "[Compaction] 添加到 L" << level + 1 
                  << ", 当前数量: " << levels_[level + 1].sstables.size() << std::endl;
        
        // 更新内存 Version
        for (const auto& old_file : input_sstables) {
            version_set_.delete_file(level, old_file.filename);
        }
        for (const auto& old_file : next_level_sstables) {
            version_set_.delete_file(level + 1, old_file.filename);
        }
        version_set_.add_file(level + 1, new_meta);
    }
    
    std::cout << "[Compaction] L" << level << " → L" << level + 1 << " 完成\n";
}

std::vector<SSTableMeta> KVDB::get_overlapping_sstables(int level, const SSTableMeta& input) {
    std::vector<SSTableMeta> result;
    
    if (level >= MAX_LEVEL) {
        return result;
    }
    
    std::lock_guard<std::mutex> lock(levels_[level].mutex);
    
    for (const auto& sstable : levels_[level].sstables) {
        if (input.overlaps_with(sstable)) {
            result.push_back(sstable);
        }
    }
    
    return result;
}

void KVDB::update_level_metadata(int level, const std::vector<SSTableMeta>& old_files) {
    std::lock_guard<std::mutex> lock(levels_[level].mutex);
    
    // 删除旧文件
    for (const auto& old_file : old_files) {
        auto it = std::find_if(levels_[level].sstables.begin(), 
                             levels_[level].sstables.end(),
                             [&](const SSTableMeta& meta) {
                                 return meta.filename == old_file.filename;
                             });
        
        if (it != levels_[level].sstables.end()) {
            levels_[level].sstables.erase(it);
            // 删除物理文件
            std::filesystem::remove(old_file.filename);
        }
    }
    
    std::cout << "[Compaction] L" << level << " 清理完成，剩余 " 
              << levels_[level].sstables.size() << " 个 SSTable\n";
}

void KVDB::execute_compaction_task(std::unique_ptr<CompactionTask> task) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 合并所有输入文件
    std::vector<SSTableMeta> all_input_files = task->input_files;
    all_input_files.insert(all_input_files.end(), 
                          task->overlapping_files.begin(), 
                          task->overlapping_files.end());
    
    if (all_input_files.empty()) {
        std::cout << "[Compaction] 没有输入文件，跳过压缩\n";
        return;
    }
    
    // 创建合并迭代器
    std::vector<std::unique_ptr<Iterator>> iterators;
    for (const auto& meta : all_input_files) {
        auto iter = std::make_unique<SSTableIterator>(meta, UINT64_MAX);
        if (iter->valid()) {
            iterators.push_back(std::move(iter));
        }
    }
    
    if (iterators.empty()) {
        std::cout << "[Compaction] 没有有效的迭代器，跳过压缩\n";
        return;
    }
    
    auto merge_iter = std::make_unique<MergeIterator>(std::move(iterators));
    
    // 创建新的 SSTable
    std::string new_filename = "data/sstable_" + std::to_string(file_id_++) + ".dat";
    
    size_t written_keys = 0;
    size_t bytes_read = 0;
    
    // 收集合并后的数据
    std::map<std::string, std::vector<VersionedValue>> merged_data;
    
    // 写入合并后的数据
    for (merge_iter->seek_to_first(); merge_iter->valid(); merge_iter->next()) {
        std::string key = merge_iter->key();
        std::string value = merge_iter->value();
        
        // 跳过墓碑记录（在压缩时清理）
        if (!value.empty()) {
            VersionedValue vv;
            vv.seq = UINT64_MAX; // 压缩后的数据使用最大序列号
            vv.value = value;
            merged_data[key].push_back(vv);
            written_keys++;
        }
        
        bytes_read += key.size() + value.size();
    }
    
    // 写入 SSTable
    SSTableWriter::write(new_filename, merged_data);
    
    // 获取新文件的元数据
    SSTableMeta new_meta = SSTableMetaUtil::get_meta_from_file(new_filename);
    size_t bytes_written = new_meta.file_size;
    
    // 更新层级结构
    {
        // 从源层级删除输入文件
        if (task->source_level < MAX_LEVEL) {
            update_level_metadata(task->source_level, task->input_files);
        }
        
        // 从目标层级删除重叠文件
        if (task->target_level < MAX_LEVEL && !task->overlapping_files.empty()) {
            update_level_metadata(task->target_level, task->overlapping_files);
        }
        
        // 将新文件添加到目标层级
        if (task->target_level < MAX_LEVEL) {
            std::lock_guard<std::mutex> lock(levels_[task->target_level].mutex);
            levels_[task->target_level].sstables.push_back(new_meta);
            std::cout << "[Compaction] 添加到 L" << task->target_level 
                      << "，当前文件数: " << levels_[task->target_level].sstables.size() << std::endl;
        }
    }
    
    // 更新版本信息 - 使用现有的 persist 方法
    version_set_.persist_add(new_meta, task->target_level);
    
    // 更新统计信息
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    {
        std::lock_guard<std::mutex> strategy_lock(compaction_strategy_mutex_);
        compaction_strategy_->stats_.update(bytes_read, bytes_written, duration);
    }
    
    std::cout << "[Compaction] 完成: 处理 " << all_input_files.size() << " 个文件, "
              << "写入 " << written_keys << " 个键, "
              << "耗时 " << duration.count() << "ms, "
              << "写放大: " << (bytes_read > 0 ? static_cast<double>(bytes_written) / bytes_read : 0.0)
              << std::endl;
}

// 缓存管理方法实现
void KVDB::enable_multi_level_cache() {
    cache_manager_->switch_to_multi_level();
    std::cout << "[KVDB] 已切换到多级缓存模式\n";
}

void KVDB::enable_legacy_cache() {
    cache_manager_->switch_to_legacy();
    std::cout << "[KVDB] 已切换到传统缓存模式\n";
}

void KVDB::warm_cache_with_hot_data(const std::vector<std::pair<std::string, std::string>>& hot_data) {
    cache_manager_->warm_cache(hot_data);
    std::cout << "[KVDB] 缓存预热完成，加载了 " << hot_data.size() << " 个热点数据\n";
}

void KVDB::print_cache_stats() const {
    cache_manager_->print_stats();
}

double KVDB::get_cache_hit_rate() const {
    return cache_manager_->get_hit_rate();
}
// 索引管理方法
bool KVDB::create_secondary_index(const std::string& name, const std::string& field, bool unique) {
    if (!index_manager_) {
        return false;
    }
    return index_manager_->create_secondary_index(name, field, unique);
}

bool KVDB::create_composite_index(const std::string& name, const std::vector<std::string>& fields) {
    if (!index_manager_) {
        return false;
    }
    return index_manager_->create_composite_index(name, fields);
}

bool KVDB::create_fulltext_index(const std::string& name, const std::string& field) {
    if (!index_manager_) {
        return false;
    }
    return index_manager_->create_fulltext_index(name, field);
}

bool KVDB::create_inverted_index(const std::string& name, const std::string& field) {
    if (!index_manager_) {
        return false;
    }
    return index_manager_->create_inverted_index(name, field);
}

bool KVDB::drop_index(const std::string& name) {
    if (!index_manager_) {
        return false;
    }
    return index_manager_->drop_index(name);
}

std::vector<IndexMetadata> KVDB::list_indexes() {
    if (!index_manager_) {
        return {};
    }
    return index_manager_->list_indexes();
}