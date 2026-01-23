#pragma once
#include "index_types.h"
#include "tokenizer.h"
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <shared_mutex>

class InvertedIndex {
public:
    // 位置信息结构
    struct PositionInfo {
        uint32_t position;      // 词在文档中的位置
        uint32_t sentence_id;   // 句子ID（可选）
        uint32_t paragraph_id;  // 段落ID（可选）
        
        PositionInfo(uint32_t pos, uint32_t sent = 0, uint32_t para = 0)
            : position(pos), sentence_id(sent), paragraph_id(para) {}
    };
    
    // 文档中的词条信息
    struct DocumentTerm {
        std::string document_id;
        uint32_t frequency;                    // 词频
        std::vector<PositionInfo> positions;   // 位置列表
        
        DocumentTerm(const std::string& doc_id) : document_id(doc_id), frequency(0) {}
    };
    
    // 倒排列表
    struct PostingList {
        std::string term;
        uint32_t document_frequency;           // 文档频率
        std::vector<DocumentTerm> documents;   // 文档列表
        
        PostingList(const std::string& t) : term(t), document_frequency(0) {}
    };
    
    explicit InvertedIndex(const std::string& name, const std::string& field);
    ~InvertedIndex();
    
    // 索引操作
    void add_document(const std::string& document_id, const std::string& text);
    void remove_document(const std::string& document_id);
    void update_document(const std::string& document_id, const std::string& old_text, const std::string& new_text);
    
    // 基本搜索
    std::vector<std::string> search_term(const std::string& term);
    std::vector<std::string> search_terms_and(const std::vector<std::string>& terms);
    std::vector<std::string> search_terms_or(const std::vector<std::string>& terms);
    
    // 短语搜索（考虑位置）
    std::vector<std::string> phrase_search(const std::vector<std::string>& terms, uint32_t max_distance = 1);
    
    // 邻近搜索
    std::vector<std::string> proximity_search(const std::vector<std::string>& terms, uint32_t max_distance);
    
    // 布尔搜索
    std::vector<std::string> boolean_search(const std::string& query);
    
    // 高级搜索结果
    struct SearchResult {
        std::string document_id;
        double score;
        std::vector<std::string> matched_terms;
        std::vector<PositionInfo> match_positions;
        
        SearchResult() : score(0.0) {}
        SearchResult(const std::string& id) : document_id(id), score(0.0) {}
    };
    
    std::vector<SearchResult> ranked_search(const std::vector<std::string>& terms, size_t limit = 10);
    
    // 获取词条信息
    const PostingList* get_posting_list(const std::string& term) const;
    std::vector<std::string> get_all_terms() const;
    
    // 统计信息
    size_t document_count() const;
    size_t term_count() const;
    size_t total_postings() const;
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
    
    // 倒排索引: term -> PostingList
    std::map<std::string, std::unique_ptr<PostingList>> index_;
    
    // 文档长度信息: document_id -> term_count
    std::map<std::string, uint32_t> document_lengths_;
    
    // 分词器
    Tokenizer tokenizer_;
    
    // 读写锁
    mutable std::shared_mutex mutex_;
    
    // 统计信息
    mutable size_t total_documents_;
    mutable size_t total_postings_;
    mutable bool stats_dirty_;
    
    // 辅助方法
    void update_stats() const;
    std::vector<std::string> tokenize_with_positions(const std::string& text, 
                                                    std::vector<uint32_t>& positions);
    
    // 内部方法（不加锁）
    void remove_document_internal(const std::string& document_id);
    
    // 搜索辅助方法
    std::vector<std::string> intersect_document_lists(const std::vector<std::vector<std::string>>& lists);
    std::vector<std::string> union_document_lists(const std::vector<std::vector<std::string>>& lists);
    bool check_phrase_match(const std::vector<std::string>& terms, 
                           const std::string& document_id, 
                           uint32_t max_distance);
    
    // 评分方法
    double calculate_bm25_score(const std::string& term, const std::string& document_id) const;
    double calculate_document_score(const std::vector<std::string>& terms, const std::string& document_id) const;
    
    // BM25 参数
    static constexpr double K1 = 1.2;
    static constexpr double B = 0.75;
    
    // 位置序列检查
    bool check_position_sequence(const std::vector<std::vector<uint32_t>>& term_positions, 
                                uint32_t max_distance) const;
};