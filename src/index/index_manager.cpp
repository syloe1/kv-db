#include "index_manager.h"
#include "db/kv_db.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <mutex>

IndexManager::IndexManager(KVDB& db) : db_(db) {
}

IndexManager::~IndexManager() = default;

bool IndexManager::create_secondary_index(const std::string& name, const std::string& field, bool unique) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index_exists(name)) {
        return false;
    }
    
    try {
        auto index = std::make_unique<SecondaryIndex>(name, field, unique);
        
        // 构建索引
        auto snapshot = db_.get_snapshot();
        auto iter = db_.new_iterator(snapshot);
        
        iter->seek_to_first();
        while (iter->valid()) {
            std::string key = iter->key();
            std::string value = iter->value();
            
            if (field == "key") {
                index->insert(key, key);
            } else if (field == "value") {
                index->insert(value, key);
            }
            
            iter->next();
        }
        
        db_.release_snapshot(snapshot);
        
        // 保存索引
        secondary_indexes_[name] = std::move(index);
        
        // 保存元数据
        IndexMetadata metadata(name, IndexType::SECONDARY, {field}, unique);
        index_metadata_[name] = metadata;
        
        update_index_stats(name);
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to create secondary index: " << e.what() << std::endl;
        return false;
    }
}

bool IndexManager::create_composite_index(const std::string& name, const std::vector<std::string>& fields) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index_exists(name) || fields.empty()) {
        return false;
    }
    
    try {
        auto index = std::make_unique<CompositeIndex>(name, fields);
        
        // 构建索引
        auto snapshot = db_.get_snapshot();
        auto iter = db_.new_iterator(snapshot);
        
        iter->seek_to_first();
        while (iter->valid()) {
            std::string key = iter->key();
            std::string value = iter->value();
            
            std::vector<std::string> field_values;
            for (const std::string& field : fields) {
                if (field == "key") {
                    field_values.push_back(key);
                } else if (field == "value") {
                    field_values.push_back(value);
                } else {
                    // 对于其他字段，可以扩展解析逻辑
                    field_values.push_back("");
                }
            }
            
            index->insert(field_values, key);
            iter->next();
        }
        
        db_.release_snapshot(snapshot);
        
        // 保存索引
        composite_indexes_[name] = std::move(index);
        
        // 保存元数据
        IndexMetadata metadata(name, IndexType::COMPOSITE, fields);
        index_metadata_[name] = metadata;
        
        update_index_stats(name);
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to create composite index: " << e.what() << std::endl;
        return false;
    }
}

bool IndexManager::create_fulltext_index(const std::string& name, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index_exists(name)) {
        return false;
    }
    
    try {
        auto index = std::make_unique<FullTextIndex>(name, field);
        
        // 构建索引
        auto snapshot = db_.get_snapshot();
        auto iter = db_.new_iterator(snapshot);
        
        iter->seek_to_first();
        while (iter->valid()) {
            std::string key = iter->key();
            std::string value = iter->value();
            
            if (field == "key") {
                index->index_document(key, key);
            } else if (field == "value") {
                index->index_document(key, value);
            }
            
            iter->next();
        }
        
        db_.release_snapshot(snapshot);
        
        // 保存索引
        fulltext_indexes_[name] = std::move(index);
        
        // 保存元数据
        IndexMetadata metadata(name, IndexType::FULLTEXT, {field});
        index_metadata_[name] = metadata;
        
        update_index_stats(name);
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to create fulltext index: " << e.what() << std::endl;
        return false;
    }
}

bool IndexManager::create_inverted_index(const std::string& name, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index_exists(name)) {
        return false;
    }
    
    try {
        auto index = std::make_unique<InvertedIndex>(name, field);
        
        // 构建索引
        auto snapshot = db_.get_snapshot();
        auto iter = db_.new_iterator(snapshot);
        
        iter->seek_to_first();
        while (iter->valid()) {
            std::string key = iter->key();
            std::string value = iter->value();
            
            if (field == "key") {
                index->add_document(key, key);
            } else if (field == "value") {
                index->add_document(key, value);
            }
            
            iter->next();
        }
        
        db_.release_snapshot(snapshot);
        
        // 保存索引
        inverted_indexes_[name] = std::move(index);
        
        // 保存元数据
        IndexMetadata metadata(name, IndexType::INVERTED, {field});
        index_metadata_[name] = metadata;
        
        update_index_stats(name);
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to create inverted index: " << e.what() << std::endl;
        return false;
    }
}
bool IndexManager::drop_index(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!index_exists(name)) {
        return false;
    }
    
    IndexType type = get_index_type(name);
    
    switch (type) {
        case IndexType::SECONDARY:
            secondary_indexes_.erase(name);
            break;
        case IndexType::COMPOSITE:
            composite_indexes_.erase(name);
            break;
        case IndexType::FULLTEXT:
            fulltext_indexes_.erase(name);
            break;
        case IndexType::INVERTED:
            inverted_indexes_.erase(name);
            break;
    }
    
    index_metadata_.erase(name);
    return true;
}

IndexLookupResult IndexManager::lookup(const std::string& index_name, const IndexQuery& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    IndexLookupResult result;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (!index_exists(index_name)) {
        result.success = false;
        result.error_message = "Index not found: " + index_name;
        return result;
    }
    
    try {
        IndexType type = get_index_type(index_name);
        
        switch (type) {
            case IndexType::SECONDARY: {
                auto& index = secondary_indexes_[index_name];
                switch (query.type) {
                    case QueryType::EXACT_MATCH:
                        result.keys = index->exact_lookup(query.value);
                        break;
                    case QueryType::RANGE_QUERY:
                        result.keys = index->range_lookup(query.range_start, query.range_end);
                        break;
                    case QueryType::PREFIX_MATCH:
                        result.keys = index->prefix_lookup(query.value);
                        break;
                    default:
                        result.success = false;
                        result.error_message = "Unsupported query type for secondary index";
                        return result;
                }
                break;
            }
            
            case IndexType::COMPOSITE: {
                auto& index = composite_indexes_[index_name];
                switch (query.type) {
                    case QueryType::EXACT_MATCH:
                        result.keys = index->exact_lookup(query.terms);
                        break;
                    case QueryType::PREFIX_MATCH:
                        result.keys = index->prefix_lookup(query.terms);
                        break;
                    case QueryType::RANGE_QUERY:
                        // 需要两个terms向量作为范围
                        if (query.terms.size() >= 2) {
                            std::vector<std::string> start_terms(query.terms.begin(), query.terms.begin() + query.terms.size()/2);
                            std::vector<std::string> end_terms(query.terms.begin() + query.terms.size()/2, query.terms.end());
                            result.keys = index->range_lookup(start_terms, end_terms);
                        }
                        break;
                    default:
                        result.success = false;
                        result.error_message = "Unsupported query type for composite index";
                        return result;
                }
                break;
            }
            
            case IndexType::FULLTEXT: {
                auto& index = fulltext_indexes_[index_name];
                switch (query.type) {
                    case QueryType::FULLTEXT_SEARCH:
                        result.keys = index->search(query.value);
                        break;
                    case QueryType::PHRASE_SEARCH:
                        result.keys = index->phrase_search(query.terms);
                        break;
                    case QueryType::BOOLEAN_SEARCH:
                        result.keys = index->boolean_search(query.value);
                        break;
                    default:
                        result.success = false;
                        result.error_message = "Unsupported query type for fulltext index";
                        return result;
                }
                break;
            }
            
            case IndexType::INVERTED: {
                auto& index = inverted_indexes_[index_name];
                switch (query.type) {
                    case QueryType::EXACT_MATCH:
                        result.keys = index->search_term(query.value);
                        break;
                    case QueryType::FULLTEXT_SEARCH:
                        result.keys = index->search_terms_and(query.terms);
                        break;
                    case QueryType::PHRASE_SEARCH:
                        result.keys = index->phrase_search(query.terms);
                        break;
                    case QueryType::BOOLEAN_SEARCH:
                        result.keys = index->boolean_search(query.value);
                        break;
                    default:
                        result.success = false;
                        result.error_message = "Unsupported query type for inverted index";
                        return result;
                }
                break;
            }
        }
        
        result.success = true;
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    result.query_time_ms = duration.count() / 1000.0;
    
    return result;
}

std::vector<std::string> IndexManager::get_applicable_indexes(const std::string& field, QueryType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> applicable_indexes;
    
    for (const auto& pair : index_metadata_) {
        const std::string& index_name = pair.first;
        const IndexMetadata& metadata = pair.second;
        
        // 检查字段匹配
        bool field_matches = false;
        for (const std::string& index_field : metadata.fields) {
            if (index_field == field) {
                field_matches = true;
                break;
            }
        }
        
        if (!field_matches) {
            continue;
        }
        
        // 检查查询类型兼容性
        bool type_compatible = false;
        switch (metadata.type) {
            case IndexType::SECONDARY:
                type_compatible = (type == QueryType::EXACT_MATCH || 
                                 type == QueryType::RANGE_QUERY || 
                                 type == QueryType::PREFIX_MATCH);
                break;
            case IndexType::COMPOSITE:
                type_compatible = (type == QueryType::EXACT_MATCH || 
                                 type == QueryType::RANGE_QUERY || 
                                 type == QueryType::PREFIX_MATCH);
                break;
            case IndexType::FULLTEXT:
                type_compatible = (type == QueryType::FULLTEXT_SEARCH || 
                                 type == QueryType::PHRASE_SEARCH || 
                                 type == QueryType::BOOLEAN_SEARCH);
                break;
            case IndexType::INVERTED:
                type_compatible = (type == QueryType::EXACT_MATCH || 
                                 type == QueryType::FULLTEXT_SEARCH || 
                                 type == QueryType::PHRASE_SEARCH || 
                                 type == QueryType::BOOLEAN_SEARCH);
                break;
        }
        
        if (type_compatible) {
            applicable_indexes.push_back(index_name);
        }
    }
    
    return applicable_indexes;
}

std::string IndexManager::choose_best_index(const std::vector<std::string>& candidates, const IndexQuery& query) {
    if (candidates.empty()) {
        return "";
    }
    
    if (candidates.size() == 1) {
        return candidates[0];
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 简单的选择策略：优先选择选择性高的索引
    std::string best_index = candidates[0];
    double best_selectivity = 0.0;
    
    for (const std::string& candidate : candidates) {
        IndexStats stats = get_index_stats(candidate);
        if (stats.selectivity > best_selectivity) {
            best_selectivity = stats.selectivity;
            best_index = candidate;
        }
    }
    
    return best_index;
}
void IndexManager::update_indexes(const std::string& key, const std::string& old_value, const std::string& new_value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 更新二级索引
    for (auto& pair : secondary_indexes_) {
        SecondaryIndex* index = pair.second.get();
        
        if (index->get_field() == "key") {
            // key字段不会改变，无需更新
            continue;
        } else if (index->get_field() == "value") {
            index->update(old_value, new_value, key);
        }
    }
    
    // 更新复合索引
    for (auto& pair : composite_indexes_) {
        CompositeIndex* index = pair.second.get();
        
        std::vector<std::string> old_values, new_values;
        for (const std::string& field : index->get_fields()) {
            if (field == "key") {
                old_values.push_back(key);
                new_values.push_back(key);
            } else if (field == "value") {
                old_values.push_back(old_value);
                new_values.push_back(new_value);
            }
        }
        
        if (old_values.size() == index->get_field_count()) {
            index->update(old_values, new_values, key);
        }
    }
    
    // 更新全文索引
    for (auto& pair : fulltext_indexes_) {
        FullTextIndex* index = pair.second.get();
        
        if (index->get_field() == "value") {
            index->update_document(key, old_value, new_value);
        }
    }
    
    // 更新倒排索引
    for (auto& pair : inverted_indexes_) {
        InvertedIndex* index = pair.second.get();
        
        if (index->get_field() == "value") {
            index->update_document(key, old_value, new_value);
        }
    }
}

void IndexManager::remove_from_indexes(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 从二级索引中移除
    for (auto& pair : secondary_indexes_) {
        SecondaryIndex* index = pair.second.get();
        
        if (index->get_field() == "key") {
            index->remove(key, key);
        } else if (index->get_field() == "value") {
            index->remove(value, key);
        }
    }
    
    // 从复合索引中移除
    for (auto& pair : composite_indexes_) {
        CompositeIndex* index = pair.second.get();
        
        std::vector<std::string> values;
        for (const std::string& field : index->get_fields()) {
            if (field == "key") {
                values.push_back(key);
            } else if (field == "value") {
                values.push_back(value);
            }
        }
        
        if (values.size() == index->get_field_count()) {
            index->remove(values, key);
        }
    }
    
    // 从全文索引中移除
    for (auto& pair : fulltext_indexes_) {
        FullTextIndex* index = pair.second.get();
        index->remove_document(key);
    }
    
    // 从倒排索引中移除
    for (auto& pair : inverted_indexes_) {
        InvertedIndex* index = pair.second.get();
        index->remove_document(key);
    }
}

void IndexManager::add_to_indexes(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 添加到二级索引
    for (auto& pair : secondary_indexes_) {
        SecondaryIndex* index = pair.second.get();
        
        if (index->get_field() == "key") {
            index->insert(key, key);
        } else if (index->get_field() == "value") {
            index->insert(value, key);
        }
    }
    
    // 添加到复合索引
    for (auto& pair : composite_indexes_) {
        CompositeIndex* index = pair.second.get();
        
        std::vector<std::string> values;
        for (const std::string& field : index->get_fields()) {
            if (field == "key") {
                values.push_back(key);
            } else if (field == "value") {
                values.push_back(value);
            }
        }
        
        if (values.size() == index->get_field_count()) {
            index->insert(values, key);
        }
    }
    
    // 添加到全文索引
    for (auto& pair : fulltext_indexes_) {
        FullTextIndex* index = pair.second.get();
        
        if (index->get_field() == "key") {
            index->index_document(key, key);
        } else if (index->get_field() == "value") {
            index->index_document(key, value);
        }
    }
    
    // 添加到倒排索引
    for (auto& pair : inverted_indexes_) {
        InvertedIndex* index = pair.second.get();
        
        if (index->get_field() == "key") {
            index->add_document(key, key);
        } else if (index->get_field() == "value") {
            index->add_document(key, value);
        }
    }
}

void IndexManager::rebuild_index(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!index_exists(name)) {
        return;
    }
    
    IndexType type = get_index_type(name);
    IndexMetadata metadata = index_metadata_[name];
    
    // 删除旧索引
    drop_index(name);
    
    // 重新创建索引
    switch (type) {
        case IndexType::SECONDARY:
            create_secondary_index(name, metadata.fields[0], metadata.is_unique);
            break;
        case IndexType::COMPOSITE:
            create_composite_index(name, metadata.fields);
            break;
        case IndexType::FULLTEXT:
            create_fulltext_index(name, metadata.fields[0]);
            break;
        case IndexType::INVERTED:
            create_inverted_index(name, metadata.fields[0]);
            break;
    }
}

void IndexManager::rebuild_all_indexes() {
    std::vector<std::string> index_names;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : index_metadata_) {
            index_names.push_back(pair.first);
        }
    }
    
    for (const std::string& name : index_names) {
        rebuild_index(name);
    }
}

IndexStats IndexManager::get_index_stats(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    IndexStats stats;
    
    if (!index_exists(name)) {
        return stats;
    }
    
    IndexType type = get_index_type(name);
    
    switch (type) {
        case IndexType::SECONDARY: {
            auto& index = secondary_indexes_[name];
            stats.total_entries = index->size();
            stats.unique_values = index->unique_values();
            stats.selectivity = index->selectivity();
            stats.memory_bytes = index->memory_usage();
            break;
        }
        case IndexType::COMPOSITE: {
            auto& index = composite_indexes_[name];
            stats.total_entries = index->size();
            stats.unique_values = index->unique_combinations();
            stats.selectivity = index->selectivity();
            stats.memory_bytes = index->memory_usage();
            break;
        }
        case IndexType::FULLTEXT: {
            auto& index = fulltext_indexes_[name];
            stats.total_entries = index->total_terms();
            stats.unique_values = index->term_count();
            stats.selectivity = stats.unique_values > 0 ? 
                static_cast<double>(stats.unique_values) / stats.total_entries : 0.0;
            stats.memory_bytes = index->memory_usage();
            break;
        }
        case IndexType::INVERTED: {
            auto& index = inverted_indexes_[name];
            stats.total_entries = index->total_postings();
            stats.unique_values = index->term_count();
            stats.selectivity = stats.unique_values > 0 ? 
                static_cast<double>(stats.unique_values) / stats.total_entries : 0.0;
            stats.memory_bytes = index->memory_usage();
            break;
        }
    }
    
    return stats;
}

std::vector<IndexMetadata> IndexManager::list_indexes() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<IndexMetadata> indexes;
    indexes.reserve(index_metadata_.size());
    
    for (const auto& pair : index_metadata_) {
        indexes.push_back(pair.second);
    }
    
    return indexes;
}

bool IndexManager::index_exists(const std::string& name) {
    return index_metadata_.find(name) != index_metadata_.end();
}

IndexType IndexManager::get_index_type(const std::string& name) {
    auto it = index_metadata_.find(name);
    if (it != index_metadata_.end()) {
        return it->second.type;
    }
    return IndexType::SECONDARY; // 默认值
}

void IndexManager::update_index_stats(const std::string& name) {
    IndexStats stats = get_index_stats(name);
    
    auto it = index_metadata_.find(name);
    if (it != index_metadata_.end()) {
        it->second.memory_usage = stats.memory_bytes;
        // disk_usage 可以在序列化时计算
    }
}