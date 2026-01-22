#include "compaction/compaction_strategy.h"
#include <algorithm>
#include <numeric>
#include <cmath>

// CompactionStrategy 基类实现
size_t CompactionStrategy::calculate_level_size(const std::vector<SSTableMeta>& level) {
    return std::accumulate(level.begin(), level.end(), 0ULL,
        [](size_t sum, const SSTableMeta& meta) {
            return sum + meta.file_size;
        });
}

std::vector<SSTableMeta> CompactionStrategy::find_overlapping_files(
    const std::vector<SSTableMeta>& level,
    const std::string& start_key,
    const std::string& end_key) {
    
    std::vector<SSTableMeta> overlapping;
    for (const auto& meta : level) {
        if (meta.max_key >= start_key && meta.min_key <= end_key) {
            overlapping.push_back(meta);
        }
    }
    return overlapping;
}

double CompactionStrategy::calculate_priority(const CompactionTask& task) {
    // 基于文件数量、大小和层级计算优先级
    double file_count_factor = static_cast<double>(task.input_files.size());
    double size_factor = static_cast<double>(task.estimated_output_size) / (1024 * 1024); // MB
    double level_factor = 1.0 / (task.source_level + 1);
    
    return file_count_factor * size_factor * level_factor;
}

// LeveledCompactionStrategy 实现
LeveledCompactionStrategy::LeveledCompactionStrategy(const std::vector<size_t>& level_limits)
    : level_size_limits_(level_limits) {
    level_compaction_scores_.resize(level_limits.size());
}

bool LeveledCompactionStrategy::needs_compaction(
    const std::vector<std::vector<SSTableMeta>>& levels) {
    
    calculate_compaction_scores(levels);
    
    // 检查是否有层级超过限制
    for (size_t i = 0; i < levels.size() && i < level_compaction_scores_.size(); i++) {
        if (level_compaction_scores_[i] > 1.0) {
            return true;
        }
    }
    return false;
}

void LeveledCompactionStrategy::calculate_compaction_scores(
    const std::vector<std::vector<SSTableMeta>>& levels) {
    
    for (size_t i = 0; i < levels.size() && i < level_size_limits_.size(); i++) {
        size_t current_size = calculate_level_size(levels[i]);
        
        if (i == 0) {
            // L0 层按文件数量计算
            level_compaction_scores_[i] = static_cast<double>(levels[i].size()) / 4.0;
        } else {
            // L1+ 层按大小计算
            level_compaction_scores_[i] = static_cast<double>(current_size) / level_size_limits_[i];
        }
    }
}

std::unique_ptr<CompactionTask> LeveledCompactionStrategy::pick_compaction(
    const std::vector<std::vector<SSTableMeta>>& levels) {
    
    calculate_compaction_scores(levels);
    
    // 找到分数最高的层级
    int max_score_level = -1;
    double max_score = 1.0;
    
    for (size_t i = 0; i < level_compaction_scores_.size(); i++) {
        if (level_compaction_scores_[i] > max_score) {
            max_score = level_compaction_scores_[i];
            max_score_level = static_cast<int>(i);
        }
    }
    
    if (max_score_level >= 0) {
        return pick_level_compaction(levels, max_score_level);
    }
    
    return nullptr;
}

std::unique_ptr<CompactionTask> LeveledCompactionStrategy::pick_level_compaction(
    const std::vector<std::vector<SSTableMeta>>& levels, int level) {
    
    if (level >= static_cast<int>(levels.size()) || levels[level].empty()) {
        return nullptr;
    }
    
    auto task = std::make_unique<CompactionTask>(level, level + 1);
    
    if (level == 0) {
        // L0 -> L1: 选择所有 L0 文件
        task->input_files = levels[0];
    } else {
        // L1+ -> L(n+1): 选择一个文件开始
        // 选择最老的文件（简化策略）
        task->input_files.push_back(levels[level][0]);
    }
    
    // 计算 key 范围
    std::string min_key = task->input_files[0].min_key;
    std::string max_key = task->input_files[0].max_key;
    
    for (const auto& file : task->input_files) {
        if (file.min_key < min_key) min_key = file.min_key;
        if (file.max_key > max_key) max_key = file.max_key;
    }
    
    // 找到目标层的重叠文件
    if (task->target_level < static_cast<int>(levels.size())) {
        task->overlapping_files = find_overlapping_files(
            levels[task->target_level], min_key, max_key);
    }
    
    // 估算输出大小
    task->estimated_output_size = calculate_level_size(task->input_files) +
                                 calculate_level_size(task->overlapping_files);
    
    task->priority = calculate_priority(*task);
    
    return task;
}

// TieredCompactionStrategy 实现
TieredCompactionStrategy::TieredCompactionStrategy(size_t max_files_per_tier)
    : max_files_per_tier_(max_files_per_tier) {
}

bool TieredCompactionStrategy::needs_compaction(
    const std::vector<std::vector<SSTableMeta>>& levels) {
    
    for (size_t i = 0; i < levels.size(); i++) {
        if (levels[i].size() >= max_files_per_tier_) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<CompactionTask> TieredCompactionStrategy::pick_compaction(
    const std::vector<std::vector<SSTableMeta>>& levels) {
    
    // 找到文件数最多的层级
    int max_files_level = -1;
    size_t max_files = max_files_per_tier_ - 1;
    
    for (size_t i = 0; i < levels.size(); i++) {
        if (levels[i].size() > max_files) {
            max_files = levels[i].size();
            max_files_level = static_cast<int>(i);
        }
    }
    
    if (max_files_level >= 0) {
        return pick_tier_compaction(levels[max_files_level], max_files_level);
    }
    
    return nullptr;
}

std::unique_ptr<CompactionTask> TieredCompactionStrategy::pick_tier_compaction(
    const std::vector<SSTableMeta>& level, int level_num) {
    
    auto task = std::make_unique<CompactionTask>(level_num, level_num);
    
    // 选择所有文件进行合并（简化策略）
    task->input_files = level;
    task->estimated_output_size = calculate_level_size(level);
    task->priority = calculate_priority(*task);
    
    return task;
}

// SizeTieredCompactionStrategy 实现
SizeTieredCompactionStrategy::SizeTieredCompactionStrategy(
    double size_ratio, size_t min_threshold)
    : size_ratio_(size_ratio), min_threshold_(min_threshold) {
}

bool SizeTieredCompactionStrategy::needs_compaction(
    const std::vector<std::vector<SSTableMeta>>& levels) {
    
    for (const auto& level : levels) {
        if (level.size() >= min_threshold_) {
            // 检查是否有相似大小的文件可以合并
            for (const auto& file : level) {
                auto similar_files = find_similar_sized_files(level, file.file_size);
                if (similar_files.size() >= min_threshold_) {
                    return true;
                }
            }
        }
    }
    return false;
}

std::unique_ptr<CompactionTask> SizeTieredCompactionStrategy::pick_compaction(
    const std::vector<std::vector<SSTableMeta>>& levels) {
    
    std::unique_ptr<CompactionTask> best_task = nullptr;
    double best_priority = 0.0;
    
    for (size_t level_num = 0; level_num < levels.size(); level_num++) {
        const auto& level = levels[level_num];
        
        if (level.size() < min_threshold_) continue;
        
        // 尝试不同的基准文件大小
        for (const auto& base_file : level) {
            auto similar_files = find_similar_sized_files(level, base_file.file_size);
            
            if (similar_files.size() >= min_threshold_) {
                auto task = std::make_unique<CompactionTask>(
                    static_cast<int>(level_num), static_cast<int>(level_num));
                task->input_files = similar_files;
                task->estimated_output_size = calculate_level_size(similar_files);
                task->priority = calculate_priority(*task);
                
                if (task->priority > best_priority) {
                    best_priority = task->priority;
                    best_task = std::move(task);
                }
            }
        }
    }
    
    return best_task;
}

std::vector<SSTableMeta> SizeTieredCompactionStrategy::find_similar_sized_files(
    const std::vector<SSTableMeta>& level, size_t base_size) {
    
    std::vector<SSTableMeta> similar_files;
    
    for (const auto& file : level) {
        double ratio = static_cast<double>(std::max(file.file_size, base_size)) /
                      static_cast<double>(std::min(file.file_size, base_size));
        
        if (ratio <= (1.0 + size_ratio_)) {
            similar_files.push_back(file);
        }
    }
    
    return similar_files;
}

// TimeWindowCompactionStrategy 实现
TimeWindowCompactionStrategy::TimeWindowCompactionStrategy(
    std::chrono::hours window_size, size_t max_files_per_window)
    : window_size_(window_size), max_files_per_window_(max_files_per_window) {
}

bool TimeWindowCompactionStrategy::needs_compaction(
    const std::vector<std::vector<SSTableMeta>>& levels) {
    
    for (const auto& level : levels) {
        auto windows = group_files_by_time_window(level);
        
        for (const auto& window : windows) {
            if (window.files.size() >= max_files_per_window_) {
                return true;
            }
        }
    }
    return false;
}

std::unique_ptr<CompactionTask> TimeWindowCompactionStrategy::pick_compaction(
    const std::vector<std::vector<SSTableMeta>>& levels) {
    
    std::unique_ptr<CompactionTask> best_task = nullptr;
    size_t max_files_in_window = 0;
    
    for (size_t level_num = 0; level_num < levels.size(); level_num++) {
        auto windows = group_files_by_time_window(levels[level_num]);
        
        for (const auto& window : windows) {
            if (window.files.size() >= max_files_per_window_ &&
                window.files.size() > max_files_in_window) {
                
                auto task = std::make_unique<CompactionTask>(
                    static_cast<int>(level_num), static_cast<int>(level_num));
                task->input_files = window.files;
                task->estimated_output_size = calculate_level_size(window.files);
                task->priority = calculate_priority(*task);
                
                max_files_in_window = window.files.size();
                best_task = std::move(task);
            }
        }
    }
    
    return best_task;
}

std::vector<TimeWindowCompactionStrategy::TimeWindow> 
TimeWindowCompactionStrategy::group_files_by_time_window(
    const std::vector<SSTableMeta>& files) {
    
    std::vector<TimeWindow> windows;
    
    for (const auto& file : files) {
        auto file_time = get_file_timestamp(file);
        
        // 找到对应的时间窗口
        bool found_window = false;
        for (auto& window : windows) {
            if (file_time >= window.start_time && file_time < window.end_time) {
                window.files.push_back(file);
                found_window = true;
                break;
            }
        }
        
        // 创建新的时间窗口
        if (!found_window) {
            TimeWindow new_window;
            auto window_start = std::chrono::time_point_cast<std::chrono::hours>(file_time);
            new_window.start_time = window_start;
            new_window.end_time = window_start + window_size_;
            new_window.files.push_back(file);
            windows.push_back(new_window);
        }
    }
    
    return windows;
}

std::chrono::system_clock::time_point TimeWindowCompactionStrategy::get_file_timestamp(
    const SSTableMeta& meta) {
    
    // 从文件名中提取时间戳
    // 假设文件名格式为 "data/sstable_<id>.dat"
    std::string filename = meta.filename;
    size_t start = filename.find("sstable_");
    if (start != std::string::npos) {
        start += 8; // "sstable_" 的长度
        size_t end = filename.find(".dat", start);
        if (end != std::string::npos) {
            std::string id_str = filename.substr(start, end - start);
            try {
                uint64_t file_id = std::stoull(id_str);
                auto timestamp = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(file_id * 1000));
                return timestamp;
            } catch (const std::exception&) {
                // 如果解析失败，使用当前时间
            }
        }
    }
    
    // 默认返回当前时间
    return std::chrono::system_clock::now();
}

// CompactionStrategyFactory 实现
std::unique_ptr<CompactionStrategy> CompactionStrategyFactory::create_strategy(
    CompactionStrategyType type, const std::vector<size_t>& level_limits) {
    
    switch (type) {
        case CompactionStrategyType::LEVELED:
            return std::make_unique<LeveledCompactionStrategy>(level_limits);
            
        case CompactionStrategyType::TIERED:
            return std::make_unique<TieredCompactionStrategy>();
            
        case CompactionStrategyType::SIZE_TIERED:
            return std::make_unique<SizeTieredCompactionStrategy>();
            
        case CompactionStrategyType::TIME_WINDOW:
            return std::make_unique<TimeWindowCompactionStrategy>();
            
        default:
            return std::make_unique<LeveledCompactionStrategy>(level_limits);
    }
}