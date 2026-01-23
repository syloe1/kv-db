#pragma once
#include "index_types.h"
#include "secondary_index.h"
#include "composite_index.h"
#include "fulltext_index.h"
#include "inverted_index.h"
#include <memory>
#include <unordered_map>
#include <mutex>

class KVDB;

class IndexManager {
public:
    explicit IndexManager(KVDB& db);
    ~IndexManager();
    
    // 索引创建和管理
    bool create_secondary_index(const std::string& name, const std::string& field, bool unique = false);
    bool create_composite_index(const std::string& name, const std::vector<std::string>& fields);
    bool create_fulltext_index(const std::string& name, const std::string& field);
    bool create_inverted_index(const std::string& name, const std::string& field);
    
    // 索引删除
    bool drop_index(const std::string& name);
    
    // 索引查询
    IndexLookupResult lookup(const std::string& index_name, const IndexQuery& query);
    
    // 查询优化
    std::vector<std::string> get_applicable_indexes(const std::string& field, QueryType type);
    std::string choose_best_index(const std::vector<std::string>& candidates, const IndexQuery& query);
    
    // 索引维护
    void update_indexes(const std::string& key, const std::string& old_value, const std::string& new_value);
    void remove_from_indexes(const std::string& key, const std::string& value);
    void add_to_indexes(const std::string& key, const std::string& value);
    
    // 批量重建
    void rebuild_index(const std::string& name);
    void rebuild_all_indexes();
    
    // 统计信息
    IndexStats get_index_stats(const std::string& name);
    std::vector<IndexMetadata> list_indexes();
    
    // 索引存储
    bool save_indexes_to_disk();
    bool load_indexes_from_disk();
    
private:
    KVDB& db_;
    std::mutex mutex_;
    
    // 索引存储
    std::unordered_map<std::string, std::unique_ptr<SecondaryIndex>> secondary_indexes_;
    std::unordered_map<std::string, std::unique_ptr<CompositeIndex>> composite_indexes_;
    std::unordered_map<std::string, std::unique_ptr<FullTextIndex>> fulltext_indexes_;
    std::unordered_map<std::string, std::unique_ptr<InvertedIndex>> inverted_indexes_;
    
    // 元数据
    std::unordered_map<std::string, IndexMetadata> index_metadata_;
    
    // 辅助方法
    bool index_exists(const std::string& name);
    IndexType get_index_type(const std::string& name);
    void update_index_stats(const std::string& name);
    
    // 序列化
    void serialize_metadata(std::ostream& out);
    void deserialize_metadata(std::istream& in);
};