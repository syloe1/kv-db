#pragma once
#include "sstable/sstable_meta.h"
#include <vector>
#include <memory>
#include <chrono>

// 压缩策略类型
enum class CompactionStrategyType {
    LEVELED,        // 分层压缩：每层无重叠，减少读放大
    TIERED,         // 分层压缩：允许重叠，减少写放大
    SIZE_TIERED,    // 按大小分层：相似大小文件合并
    TIME_WINDOW     // 时间窗口：按时间范围合并，适合时序数据
};

// 压缩任务
struct CompactionTask {
    int source_level;
    int target_level;
    std::vector<SSTableMeta> input_files;
    std::vector<SSTableMeta> overlapping_files;  // 目标层重叠文件
    size_t estimated_output_size;
    double priority;  // 压缩优先级
    
    CompactionTask(int src_level, int tgt_level) 
        : source_level(src_level), target_level(tgt_level), 
          estimated_output_size(0), priority(0.0) {}
};

// 压缩统计信息
struct CompactionStats {
    size_t total_compactions = 0;
    size_t bytes_written = 0;
    size_t bytes_read = 0;
    double write_amplification = 0.0;
    double read_amplification = 0.0;
    std::chrono::milliseconds total_time{0};
    
    void update(size_t read_bytes, size_t written_bytes, 
                std::chrono::milliseconds duration) {
        total_compactions++;
        bytes_read += read_bytes;
        bytes_written += written_bytes;
        total_time += duration;
        
        // 计算放大系数
        if (bytes_read > 0) {
            write_amplification = static_cast<double>(bytes_written) / bytes_read;
        }
    }
};

// 抽象压缩策略基类
class CompactionStrategy {
public:
    virtual ~CompactionStrategy() = default;
    
    // 检查是否需要压缩
    virtual bool needs_compaction(const std::vector<std::vector<SSTableMeta>>& levels) = 0;
    
    // 选择压缩任务
    virtual std::unique_ptr<CompactionTask> pick_compaction(
        const std::vector<std::vector<SSTableMeta>>& levels) = 0;
    
    // 获取策略类型
    virtual CompactionStrategyType get_type() const = 0;
    
    // 获取统计信息
    const CompactionStats& get_stats() const { return stats_; }
    
    // 重置统计信息
    void reset_stats() { stats_ = CompactionStats{}; }

protected:
    CompactionStats stats_;
    
    // 允许 KVDB 访问统计信息
    friend class KVDB;
    
    // 辅助函数
    size_t calculate_level_size(const std::vector<SSTableMeta>& level);
    std::vector<SSTableMeta> find_overlapping_files(
        const std::vector<SSTableMeta>& level, 
        const std::string& start_key, 
        const std::string& end_key);
    double calculate_priority(const CompactionTask& task);
};

// 分层压缩策略 (Leveled Compaction)
class LeveledCompactionStrategy : public CompactionStrategy {
public:
    explicit LeveledCompactionStrategy(const std::vector<size_t>& level_limits);
    
    bool needs_compaction(const std::vector<std::vector<SSTableMeta>>& levels) override;
    std::unique_ptr<CompactionTask> pick_compaction(
        const std::vector<std::vector<SSTableMeta>>& levels) override;
    CompactionStrategyType get_type() const override { return CompactionStrategyType::LEVELED; }

private:
    std::vector<size_t> level_size_limits_;
    std::vector<size_t> level_compaction_scores_;
    
    void calculate_compaction_scores(const std::vector<std::vector<SSTableMeta>>& levels);
    std::unique_ptr<CompactionTask> pick_level_compaction(
        const std::vector<std::vector<SSTableMeta>>& levels, int level);
};

// 分层压缩策略 (Tiered Compaction)
class TieredCompactionStrategy : public CompactionStrategy {
public:
    explicit TieredCompactionStrategy(size_t max_files_per_tier = 4);
    
    bool needs_compaction(const std::vector<std::vector<SSTableMeta>>& levels) override;
    std::unique_ptr<CompactionTask> pick_compaction(
        const std::vector<std::vector<SSTableMeta>>& levels) override;
    CompactionStrategyType get_type() const override { return CompactionStrategyType::TIERED; }

private:
    size_t max_files_per_tier_;
    
    std::unique_ptr<CompactionTask> pick_tier_compaction(
        const std::vector<SSTableMeta>& level, int level_num);
};

// 按大小分层压缩策略 (Size-Tiered Compaction)
class SizeTieredCompactionStrategy : public CompactionStrategy {
public:
    explicit SizeTieredCompactionStrategy(double size_ratio = 0.5, size_t min_threshold = 4);
    
    bool needs_compaction(const std::vector<std::vector<SSTableMeta>>& levels) override;
    std::unique_ptr<CompactionTask> pick_compaction(
        const std::vector<std::vector<SSTableMeta>>& levels) override;
    CompactionStrategyType get_type() const override { return CompactionStrategyType::SIZE_TIERED; }

private:
    double size_ratio_;      // 大小比例阈值
    size_t min_threshold_;   // 最小合并文件数
    
    std::vector<SSTableMeta> find_similar_sized_files(
        const std::vector<SSTableMeta>& level, size_t base_size);
};

// 时间窗口压缩策略 (Time Window Compaction)
class TimeWindowCompactionStrategy : public CompactionStrategy {
public:
    explicit TimeWindowCompactionStrategy(
        std::chrono::hours window_size = std::chrono::hours(24),
        size_t max_files_per_window = 10);
    
    bool needs_compaction(const std::vector<std::vector<SSTableMeta>>& levels) override;
    std::unique_ptr<CompactionTask> pick_compaction(
        const std::vector<std::vector<SSTableMeta>>& levels) override;
    CompactionStrategyType get_type() const override { return CompactionStrategyType::TIME_WINDOW; }

private:
    std::chrono::hours window_size_;
    size_t max_files_per_window_;
    
    struct TimeWindow {
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        std::vector<SSTableMeta> files;
    };
    
    std::vector<TimeWindow> group_files_by_time_window(
        const std::vector<SSTableMeta>& files);
    std::chrono::system_clock::time_point get_file_timestamp(const SSTableMeta& meta);
};

// 压缩策略工厂
class CompactionStrategyFactory {
public:
    static std::unique_ptr<CompactionStrategy> create_strategy(
        CompactionStrategyType type,
        const std::vector<size_t>& level_limits = {});
};