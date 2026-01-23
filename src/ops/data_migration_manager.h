#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>

namespace kvdb {
namespace ops {

// 数据迁移状态
enum class MigrationStatus {
    PENDING,
    RUNNING,
    PAUSED,
    COMPLETED,
    FAILED,
    CANCELLED
};

// 迁移任务信息
struct MigrationTask {
    std::string task_id;
    std::string source_path;
    std::string target_path;
    std::string format; // json, binary, csv, etc.
    MigrationStatus status;
    size_t total_records;
    size_t processed_records;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::string error_message;
    
    double get_progress() const {
        if (total_records == 0) return 0.0;
        return static_cast<double>(processed_records) / total_records * 100.0;
    }
};

// 数据导出格式
enum class ExportFormat {
    JSON,
    CSV,
    BINARY,
    PROTOBUF,
    MESSAGEPACK
};

// 数据导入格式
enum class ImportFormat {
    JSON,
    CSV,
    BINARY,
    PROTOBUF,
    MESSAGEPACK,
    AUTO_DETECT
};

// 迁移配置
struct MigrationConfig {
    size_t batch_size = 1000;
    size_t max_concurrent_tasks = 4;
    bool enable_compression = true;
    bool enable_checksum = true;
    bool enable_incremental = false;
    std::string compression_algorithm = "snappy";
    
    // 过滤条件
    std::string key_pattern;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
};

// 进度回调函数
using ProgressCallback = std::function<void(const MigrationTask&)>;

class DataMigrationManager {
public:
    DataMigrationManager();
    ~DataMigrationManager();

    // 数据导出
    std::string export_data(const std::string& db_path,
                           const std::string& output_path,
                           ExportFormat format,
                           const MigrationConfig& config = {},
                           ProgressCallback callback = nullptr);

    // 数据导入
    std::string import_data(const std::string& input_path,
                           const std::string& db_path,
                           ImportFormat format,
                           const MigrationConfig& config = {},
                           ProgressCallback callback = nullptr);

    // 数据库间迁移
    std::string migrate_database(const std::string& source_db,
                                const std::string& target_db,
                                const MigrationConfig& config = {},
                                ProgressCallback callback = nullptr);

    // 增量同步
    std::string start_incremental_sync(const std::string& source_db,
                                      const std::string& target_db,
                                      const MigrationConfig& config = {});

    // 任务管理
    bool pause_task(const std::string& task_id);
    bool resume_task(const std::string& task_id);
    bool cancel_task(const std::string& task_id);
    MigrationTask get_task_status(const std::string& task_id);
    std::vector<MigrationTask> list_tasks();

    // 数据验证
    bool verify_migration(const std::string& source_path,
                         const std::string& target_path);

    // 清理任务
    void cleanup_completed_tasks();
    void cleanup_all_tasks();

private:
    std::unordered_map<std::string, std::unique_ptr<MigrationTask>> tasks_;
    std::mutex tasks_mutex_;
    std::atomic<bool> shutdown_;
    std::vector<std::thread> worker_threads_;

    // 内部方法
    std::string generate_task_id();
    void run_export_task(std::unique_ptr<MigrationTask> task,
                        const std::string& db_path,
                        ExportFormat format,
                        const MigrationConfig& config,
                        ProgressCallback callback);
    void run_import_task(std::unique_ptr<MigrationTask> task,
                        const std::string& db_path,
                        ImportFormat format,
                        const MigrationConfig& config,
                        ProgressCallback callback);
    void run_migration_task(std::unique_ptr<MigrationTask> task,
                           const std::string& source_db,
                           const std::string& target_db,
                           const MigrationConfig& config,
                           ProgressCallback callback);

    // 格式处理
    bool export_to_json(const std::string& db_path,
                       const std::string& output_path,
                       const MigrationConfig& config,
                       MigrationTask* task);
    bool export_to_csv(const std::string& db_path,
                      const std::string& output_path,
                      const MigrationConfig& config,
                      MigrationTask* task);
    bool import_from_json(const std::string& input_path,
                         const std::string& db_path,
                         const MigrationConfig& config,
                         MigrationTask* task);
    bool import_from_csv(const std::string& input_path,
                        const std::string& db_path,
                        const MigrationConfig& config,
                        MigrationTask* task);

    // 工具方法
    ImportFormat detect_format(const std::string& file_path);
    bool validate_file_format(const std::string& file_path, ImportFormat format);
    size_t count_records(const std::string& file_path, ImportFormat format);
};

} // namespace ops
} // namespace kvdb