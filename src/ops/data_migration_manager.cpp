#include "data_migration_manager.h"
#include "../db/kv_db.h"
#include "../storage/json_serializer.h"
#include <fstream>
#include <sstream>
#include <random>
#include <iomanip>
#include <filesystem>
#include <algorithm>

namespace kvdb {
namespace ops {

DataMigrationManager::DataMigrationManager() : shutdown_(false) {
    // 启动工作线程池
    size_t num_threads = std::thread::hardware_concurrency();
    for (size_t i = 0; i < num_threads; ++i) {
        worker_threads_.emplace_back([this]() {
            // 工作线程逻辑可以在这里实现
        });
    }
}

DataMigrationManager::~DataMigrationManager() {
    shutdown_ = true;
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

std::string DataMigrationManager::export_data(const std::string& db_path,
                                             const std::string& output_path,
                                             ExportFormat format,
                                             const MigrationConfig& config,
                                             ProgressCallback callback) {
    auto task = std::make_unique<MigrationTask>();
    task->task_id = generate_task_id();
    task->source_path = db_path;
    task->target_path = output_path;
    task->status = MigrationStatus::PENDING;
    task->start_time = std::chrono::system_clock::now();
    
    std::string task_id = task->task_id;
    
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_[task_id] = std::move(task);
    }
    
    // 异步执行导出任务
    std::thread([this, task_id, db_path, output_path, format, config, callback]() {
        std::unique_ptr<MigrationTask> task;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            task = std::move(tasks_[task_id]);
        }
        
        run_export_task(std::move(task), db_path, format, config, callback);
    }).detach();
    
    return task_id;
}

std::string DataMigrationManager::import_data(const std::string& input_path,
                                             const std::string& db_path,
                                             ImportFormat format,
                                             const MigrationConfig& config,
                                             ProgressCallback callback) {
    auto task = std::make_unique<MigrationTask>();
    task->task_id = generate_task_id();
    task->source_path = input_path;
    task->target_path = db_path;
    task->status = MigrationStatus::PENDING;
    task->start_time = std::chrono::system_clock::now();
    
    // 自动检测格式
    if (format == ImportFormat::AUTO_DETECT) {
        format = detect_format(input_path);
    }
    
    // 计算总记录数
    task->total_records = count_records(input_path, format);
    
    std::string task_id = task->task_id;
    
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_[task_id] = std::move(task);
    }
    
    // 异步执行导入任务
    std::thread([this, task_id, input_path, db_path, format, config, callback]() {
        std::unique_ptr<MigrationTask> task;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            task = std::move(tasks_[task_id]);
        }
        
        run_import_task(std::move(task), db_path, format, config, callback);
    }).detach();
    
    return task_id;
}

std::string DataMigrationManager::migrate_database(const std::string& source_db,
                                                  const std::string& target_db,
                                                  const MigrationConfig& config,
                                                  ProgressCallback callback) {
    auto task = std::make_unique<MigrationTask>();
    task->task_id = generate_task_id();
    task->source_path = source_db;
    task->target_path = target_db;
    task->status = MigrationStatus::PENDING;
    task->start_time = std::chrono::system_clock::now();
    
    std::string task_id = task->task_id;
    
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_[task_id] = std::move(task);
    }
    
    // 异步执行迁移任务
    std::thread([this, task_id, source_db, target_db, config, callback]() {
        std::unique_ptr<MigrationTask> task;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            task = std::move(tasks_[task_id]);
        }
        
        run_migration_task(std::move(task), source_db, target_db, config, callback);
    }).detach();
    
    return task_id;
}

bool DataMigrationManager::pause_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end() && it->second->status == MigrationStatus::RUNNING) {
        it->second->status = MigrationStatus::PAUSED;
        return true;
    }
    return false;
}

bool DataMigrationManager::resume_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end() && it->second->status == MigrationStatus::PAUSED) {
        it->second->status = MigrationStatus::RUNNING;
        return true;
    }
    return false;
}

bool DataMigrationManager::cancel_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end() && 
        (it->second->status == MigrationStatus::RUNNING || 
         it->second->status == MigrationStatus::PAUSED)) {
        it->second->status = MigrationStatus::CANCELLED;
        return true;
    }
    return false;
}

MigrationTask DataMigrationManager::get_task_status(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return *it->second;
    }
    return MigrationTask{}; // 返回空任务
}

std::vector<MigrationTask> DataMigrationManager::list_tasks() {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    std::vector<MigrationTask> result;
    for (const auto& pair : tasks_) {
        result.push_back(*pair.second);
    }
    return result;
}

std::string DataMigrationManager::generate_task_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "task_";
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

void DataMigrationManager::run_export_task(std::unique_ptr<MigrationTask> task,
                                          const std::string& db_path,
                                          ExportFormat format,
                                          const MigrationConfig& config,
                                          ProgressCallback callback) {
    task->status = MigrationStatus::RUNNING;
    
    bool success = false;
    try {
        switch (format) {
            case ExportFormat::JSON:
                success = export_to_json(db_path, task->target_path, config, task.get());
                break;
            case ExportFormat::CSV:
                success = export_to_csv(db_path, task->target_path, config, task.get());
                break;
            default:
                task->error_message = "Unsupported export format";
                break;
        }
    } catch (const std::exception& e) {
        task->error_message = e.what();
        success = false;
    }
    
    task->status = success ? MigrationStatus::COMPLETED : MigrationStatus::FAILED;
    task->end_time = std::chrono::system_clock::now();
    
    if (callback) {
        callback(*task);
    }
    
    // 将任务放回管理器
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_[task->task_id] = std::move(task);
    }
}

bool DataMigrationManager::export_to_json(const std::string& db_path,
                                         const std::string& output_path,
                                         const MigrationConfig& config,
                                         MigrationTask* task) {
    try {
        KvDB db(db_path);
        std::ofstream output(output_path);
        if (!output.is_open()) {
            task->error_message = "Cannot open output file: " + output_path;
            return false;
        }
        
        output << "[\n";
        bool first = true;
        size_t count = 0;
        
        // 遍历数据库中的所有键值对
        auto iterator = db.create_iterator();
        iterator->seek_to_first();
        
        while (iterator->valid()) {
            if (task->status == MigrationStatus::CANCELLED) {
                return false;
            }
            
            // 暂停处理
            while (task->status == MigrationStatus::PAUSED) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            std::string key = iterator->key();
            std::string value = iterator->value();
            
            // 应用过滤条件
            if (!config.key_pattern.empty()) {
                if (key.find(config.key_pattern) == std::string::npos) {
                    iterator->next();
                    continue;
                }
            }
            
            if (!first) {
                output << ",\n";
            }
            first = false;
            
            output << "  {\"key\": \"" << key << "\", \"value\": \"" << value << "\"}";
            
            count++;
            task->processed_records = count;
            
            if (count % config.batch_size == 0) {
                output.flush();
            }
            
            iterator->next();
        }
        
        output << "\n]";
        output.close();
        
        task->total_records = count;
        return true;
        
    } catch (const std::exception& e) {
        task->error_message = e.what();
        return false;
    }
}

bool DataMigrationManager::export_to_csv(const std::string& db_path,
                                        const std::string& output_path,
                                        const MigrationConfig& config,
                                        MigrationTask* task) {
    try {
        KvDB db(db_path);
        std::ofstream output(output_path);
        if (!output.is_open()) {
            task->error_message = "Cannot open output file: " + output_path;
            return false;
        }
        
        // CSV 头部
        output << "key,value\n";
        
        size_t count = 0;
        auto iterator = db.create_iterator();
        iterator->seek_to_first();
        
        while (iterator->valid()) {
            if (task->status == MigrationStatus::CANCELLED) {
                return false;
            }
            
            while (task->status == MigrationStatus::PAUSED) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            std::string key = iterator->key();
            std::string value = iterator->value();
            
            // 转义CSV特殊字符
            auto escape_csv = [](const std::string& str) {
                std::string result = str;
                if (result.find(',') != std::string::npos || 
                    result.find('"') != std::string::npos ||
                    result.find('\n') != std::string::npos) {
                    // 替换双引号
                    size_t pos = 0;
                    while ((pos = result.find('"', pos)) != std::string::npos) {
                        result.replace(pos, 1, "\"\"");
                        pos += 2;
                    }
                    result = "\"" + result + "\"";
                }
                return result;
            };
            
            output << escape_csv(key) << "," << escape_csv(value) << "\n";
            
            count++;
            task->processed_records = count;
            
            if (count % config.batch_size == 0) {
                output.flush();
            }
            
            iterator->next();
        }
        
        output.close();
        task->total_records = count;
        return true;
        
    } catch (const std::exception& e) {
        task->error_message = e.what();
        return false;
    }
}

ImportFormat DataMigrationManager::detect_format(const std::string& file_path) {
    std::string extension = std::filesystem::path(file_path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    if (extension == ".json") {
        return ImportFormat::JSON;
    } else if (extension == ".csv") {
        return ImportFormat::CSV;
    } else if (extension == ".bin" || extension == ".dat") {
        return ImportFormat::BINARY;
    }
    
    return ImportFormat::JSON; // 默认格式
}

size_t DataMigrationManager::count_records(const std::string& file_path, ImportFormat format) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return 0;
    }
    
    size_t count = 0;
    std::string line;
    
    switch (format) {
        case ImportFormat::JSON:
            // 简单计算JSON数组中的对象数量
            while (std::getline(file, line)) {
                if (line.find("\"key\"") != std::string::npos) {
                    count++;
                }
            }
            break;
            
        case ImportFormat::CSV:
            // 计算CSV行数（减去头部）
            while (std::getline(file, line)) {
                count++;
            }
            if (count > 0) count--; // 减去头部行
            break;
            
        default:
            count = 1000; // 默认估计值
            break;
    }
    
    return count;
}

void DataMigrationManager::cleanup_completed_tasks() {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto it = tasks_.begin();
    while (it != tasks_.end()) {
        if (it->second->status == MigrationStatus::COMPLETED ||
            it->second->status == MigrationStatus::FAILED ||
            it->second->status == MigrationStatus::CANCELLED) {
            it = tasks_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace ops
} // namespace kvdb