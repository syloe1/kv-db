#pragma once
#include "index_types.h"
#include "tokenizer.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <memory>
#include <shared_mutex>

class FullTextIndex {
public:
    explicit FullTextIndex(const std::string& name, const std::string& field);
    ~FullTextIndex();
    
    // 索引操作
    void index_document(const std::string& document_id, const std::string& text);
    void remove_document(const std::string& document_id);
    void update_document(const std::string& document_id, const std::string& old_text, const std::string& new_text);
    
    // 搜索操作
    std::vector<std::string> search(const std::string& query);
    std::vector<std::string> phrase_search(const std::vector<std::string>& terms);
    std::vector<std::string> boolean_search(const std::string& query);
    std::vector<std::string> wildcard_search(const std::string& pattern);
    
    // 高级搜索
    struct SearchResult {
        std::string document_id;
        double score;
        std::vector<std::string> matched_terms;
        
        SearchResult() : score(0.0) {}
        SearchResult(const std::string& id, double s) : document_id(id), score(s) {}
    };
    
    std::vector<SearchResult> ranked_search(const std::string& query, size_t limit = 10);
    
    // 统计信息
    size_t document_count() const;
    size_t term_count() const;
    size_t total_terms() const;
    double average_document_length() const;
    size_t memory_usage() const;
    
    // 序列化
    void serialize(std::ostream& out) const;
    void deserialize(std::istream& in);
    
    // 清空索引
    void clear();
    
    // 配置
    Tokenizer& get_tokenizer() { return tokenizer_; }
    const Tokenizer& get_tokenizer() const { return tokenizer_; }
    
    const std::string& get_name() const { return name_; }
    const std::string& get_field() const { return field_; }
    
private:
    std::string name_;
    std::string field_;
    
    // 倒排索引: term -> set<document_id>
    std::map<std::string, std::set<std::string>> inverted_index_;
    
    // 文档信息: document_id -> document_info
    struct DocumentInfo {
        std::set<std::string> terms;
        size_t term_count;
        
        DocumentInfo() : term_count(0) {}
    };
    std::map<std::string, DocumentInfo> document_info_;
    
    // 词频统计: term -> document_frequency
    std::map<std::string, size_t> term_frequencies_;
    
    // 分词器
    Tokenizer tokenizer_;
    
    // 读写锁
    mutable std::shared_mutex mutex_;
    
    // 统计信息
    mutable size_t total_terms_;
    mutable bool stats_dirty_;
    
    // 辅助方法
    void update_stats() const;
    double calculate_tf_idf(const std::string& term, const std::string& document_id) const;
    double calculate_document_score(const std::string& document_id, const std::vector<std::string>& query_terms) const;
    std::vector<std::string> parse_boolean_query(const std::string& query);
    bool match_wildcard(const std::string& text, const std::string& pattern);
    
    // TF-IDF 计算
    double term_frequency(const std::string& term, const std::string& document_id) const;
    double inverse_document_frequency(const std::string& term) const;
};