#include "fulltext_index.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <regex>
#include <mutex>
#include <shared_mutex>

FullTextIndex::FullTextIndex(const std::string& name, const std::string& field)
    : name_(name), field_(field), total_terms_(0), stats_dirty_(true) {
}

FullTextIndex::~FullTextIndex() = default;

void FullTextIndex::index_document(const std::string& document_id, const std::string& text) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 如果文档已存在，先移除
    auto doc_it = document_info_.find(document_id);
    if (doc_it != document_info_.end()) {
        // 从倒排索引中移除旧的词条
        for (const std::string& term : doc_it->second.terms) {
            auto inv_it = inverted_index_.find(term);
            if (inv_it != inverted_index_.end()) {
                inv_it->second.erase(document_id);
                if (inv_it->second.empty()) {
                    inverted_index_.erase(inv_it);
                    term_frequencies_.erase(term);
                }
            }
        }
    }
    
    // 分词
    std::vector<std::string> terms = tokenizer_.tokenize(text);
    
    // 创建文档信息
    DocumentInfo doc_info;
    doc_info.term_count = terms.size();
    
    // 建立倒排索引
    for (const std::string& term : terms) {
        inverted_index_[term].insert(document_id);
        doc_info.terms.insert(term);
        term_frequencies_[term]++;
    }
    
    document_info_[document_id] = std::move(doc_info);
    stats_dirty_ = true;
}

void FullTextIndex::remove_document(const std::string& document_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto doc_it = document_info_.find(document_id);
    if (doc_it == document_info_.end()) {
        return;
    }
    
    // 从倒排索引中移除
    for (const std::string& term : doc_it->second.terms) {
        auto inv_it = inverted_index_.find(term);
        if (inv_it != inverted_index_.end()) {
            inv_it->second.erase(document_id);
            if (inv_it->second.empty()) {
                inverted_index_.erase(inv_it);
                term_frequencies_.erase(term);
            } else {
                term_frequencies_[term]--;
            }
        }
    }
    
    document_info_.erase(doc_it);
    stats_dirty_ = true;
}

void FullTextIndex::update_document(const std::string& document_id, 
                                   const std::string& old_text, 
                                   const std::string& new_text) {
    if (old_text == new_text) {
        return;
    }
    
    // 重新索引文档
    index_document(document_id, new_text);
}

std::vector<std::string> FullTextIndex::search(const std::string& query) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> query_terms = tokenizer_.tokenize(query);
    if (query_terms.empty()) {
        return {};
    }
    
    std::set<std::string> result_set;
    bool first_term = true;
    
    for (const std::string& term : query_terms) {
        auto it = inverted_index_.find(term);
        if (it == inverted_index_.end()) {
            // 如果任何一个词不存在，返回空结果（AND 语义）
            return {};
        }
        
        if (first_term) {
            result_set = it->second;
            first_term = false;
        } else {
            // 求交集
            std::set<std::string> intersection;
            std::set_intersection(result_set.begin(), result_set.end(),
                                it->second.begin(), it->second.end(),
                                std::inserter(intersection, intersection.begin()));
            result_set = std::move(intersection);
        }
        
        if (result_set.empty()) {
            break;
        }
    }
    
    return std::vector<std::string>(result_set.begin(), result_set.end());
}

std::vector<std::string> FullTextIndex::phrase_search(const std::vector<std::string>& terms) {
    if (terms.empty()) {
        return {};
    }
    
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    // 先找到包含所有词的文档
    std::set<std::string> candidate_docs;
    bool first_term = true;
    
    for (const std::string& term : terms) {
        std::string normalized_term = tokenizer_.normalize(term);
        auto it = inverted_index_.find(normalized_term);
        if (it == inverted_index_.end()) {
            return {};
        }
        
        if (first_term) {
            candidate_docs = it->second;
            first_term = false;
        } else {
            std::set<std::string> intersection;
            std::set_intersection(candidate_docs.begin(), candidate_docs.end(),
                                it->second.begin(), it->second.end(),
                                std::inserter(intersection, intersection.begin()));
            candidate_docs = std::move(intersection);
        }
        
        if (candidate_docs.empty()) {
            break;
        }
    }
    
    // 注意：这里简化了短语搜索，实际应该检查词的位置
    // 在完整实现中，需要存储词的位置信息
    return std::vector<std::string>(candidate_docs.begin(), candidate_docs.end());
}

std::vector<std::string> FullTextIndex::boolean_search(const std::string& query) {
    // 简化的布尔搜索实现
    // 支持 AND, OR, NOT 操作符
    std::vector<std::string> terms = parse_boolean_query(query);
    
    if (terms.empty()) {
        return {};
    }
    
    // 这里简化为普通搜索
    return search(query);
}

std::vector<std::string> FullTextIndex::wildcard_search(const std::string& pattern) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::set<std::string> result_set;
    
    for (const auto& pair : inverted_index_) {
        if (match_wildcard(pair.first, pattern)) {
            result_set.insert(pair.second.begin(), pair.second.end());
        }
    }
    
    return std::vector<std::string>(result_set.begin(), result_set.end());
}

std::vector<FullTextIndex::SearchResult> FullTextIndex::ranked_search(const std::string& query, size_t limit) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> query_terms = tokenizer_.tokenize(query);
    if (query_terms.empty()) {
        return {};
    }
    
    // 收集候选文档
    std::set<std::string> candidate_docs;
    for (const std::string& term : query_terms) {
        auto it = inverted_index_.find(term);
        if (it != inverted_index_.end()) {
            candidate_docs.insert(it->second.begin(), it->second.end());
        }
    }
    
    // 计算每个文档的得分
    std::vector<SearchResult> results;
    for (const std::string& doc_id : candidate_docs) {
        double score = calculate_document_score(doc_id, query_terms);
        if (score > 0) {
            SearchResult result(doc_id, score);
            // 找到匹配的词条
            for (const std::string& term : query_terms) {
                auto it = inverted_index_.find(term);
                if (it != inverted_index_.end() && it->second.count(doc_id) > 0) {
                    result.matched_terms.push_back(term);
                }
            }
            results.push_back(result);
        }
    }
    
    // 按得分排序
    std::sort(results.begin(), results.end(), 
              [](const SearchResult& a, const SearchResult& b) {
                  return a.score > b.score;
              });
    
    // 限制结果数量
    if (limit > 0 && results.size() > limit) {
        results.resize(limit);
    }
    
    return results;
}

size_t FullTextIndex::document_count() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return document_info_.size();
}

size_t FullTextIndex::term_count() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return inverted_index_.size();
}

size_t FullTextIndex::total_terms() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (stats_dirty_) {
        update_stats();
    }
    return total_terms_;
}

double FullTextIndex::average_document_length() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (document_info_.empty()) return 0.0;
    
    size_t total_length = 0;
    for (const auto& pair : document_info_) {
        total_length += pair.second.term_count;
    }
    
    return static_cast<double>(total_length) / document_info_.size();
}

size_t FullTextIndex::memory_usage() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    size_t usage = sizeof(*this);
    
    // 倒排索引
    for (const auto& pair : inverted_index_) {
        usage += pair.first.size();
        usage += sizeof(std::set<std::string>);
        for (const auto& doc_id : pair.second) {
            usage += doc_id.size();
        }
    }
    
    // 文档信息
    for (const auto& pair : document_info_) {
        usage += pair.first.size();
        usage += sizeof(DocumentInfo);
        for (const auto& term : pair.second.terms) {
            usage += term.size();
        }
    }
    
    return usage;
}

void FullTextIndex::serialize(std::ostream& out) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    // 写入基本信息
    size_t name_len = name_.size();
    out.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
    out.write(name_.c_str(), name_len);
    
    size_t field_len = field_.size();
    out.write(reinterpret_cast<const char*>(&field_len), sizeof(field_len));
    out.write(field_.c_str(), field_len);
    
    // 写入倒排索引
    size_t index_size = inverted_index_.size();
    out.write(reinterpret_cast<const char*>(&index_size), sizeof(index_size));
    
    for (const auto& pair : inverted_index_) {
        // 写入词条
        size_t term_len = pair.first.size();
        out.write(reinterpret_cast<const char*>(&term_len), sizeof(term_len));
        out.write(pair.first.c_str(), term_len);
        
        // 写入文档集合
        size_t doc_count = pair.second.size();
        out.write(reinterpret_cast<const char*>(&doc_count), sizeof(doc_count));
        
        for (const auto& doc_id : pair.second) {
            size_t doc_len = doc_id.size();
            out.write(reinterpret_cast<const char*>(&doc_len), sizeof(doc_len));
            out.write(doc_id.c_str(), doc_len);
        }
    }
    
    // 写入文档信息
    size_t doc_info_size = document_info_.size();
    out.write(reinterpret_cast<const char*>(&doc_info_size), sizeof(doc_info_size));
    
    for (const auto& pair : document_info_) {
        // 写入文档ID
        size_t doc_id_len = pair.first.size();
        out.write(reinterpret_cast<const char*>(&doc_id_len), sizeof(doc_id_len));
        out.write(pair.first.c_str(), doc_id_len);
        
        // 写入文档信息
        out.write(reinterpret_cast<const char*>(&pair.second.term_count), sizeof(pair.second.term_count));
        
        size_t terms_count = pair.second.terms.size();
        out.write(reinterpret_cast<const char*>(&terms_count), sizeof(terms_count));
        
        for (const auto& term : pair.second.terms) {
            size_t term_len = term.size();
            out.write(reinterpret_cast<const char*>(&term_len), sizeof(term_len));
            out.write(term.c_str(), term_len);
        }
    }
}

void FullTextIndex::deserialize(std::istream& in) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 读取基本信息
    size_t name_len;
    in.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
    name_.resize(name_len);
    in.read(&name_[0], name_len);
    
    size_t field_len;
    in.read(reinterpret_cast<char*>(&field_len), sizeof(field_len));
    field_.resize(field_len);
    in.read(&field_[0], field_len);
    
    // 读取倒排索引
    inverted_index_.clear();
    size_t index_size;
    in.read(reinterpret_cast<char*>(&index_size), sizeof(index_size));
    
    for (size_t i = 0; i < index_size; ++i) {
        // 读取词条
        size_t term_len;
        in.read(reinterpret_cast<char*>(&term_len), sizeof(term_len));
        std::string term(term_len, '\0');
        in.read(&term[0], term_len);
        
        // 读取文档集合
        size_t doc_count;
        in.read(reinterpret_cast<char*>(&doc_count), sizeof(doc_count));
        
        std::set<std::string> doc_set;
        for (size_t j = 0; j < doc_count; ++j) {
            size_t doc_len;
            in.read(reinterpret_cast<char*>(&doc_len), sizeof(doc_len));
            std::string doc_id(doc_len, '\0');
            in.read(&doc_id[0], doc_len);
            doc_set.insert(doc_id);
        }
        
        inverted_index_[term] = std::move(doc_set);
    }
    
    // 读取文档信息
    document_info_.clear();
    size_t doc_info_size;
    in.read(reinterpret_cast<char*>(&doc_info_size), sizeof(doc_info_size));
    
    for (size_t i = 0; i < doc_info_size; ++i) {
        // 读取文档ID
        size_t doc_id_len;
        in.read(reinterpret_cast<char*>(&doc_id_len), sizeof(doc_id_len));
        std::string doc_id(doc_id_len, '\0');
        in.read(&doc_id[0], doc_id_len);
        
        // 读取文档信息
        DocumentInfo doc_info;
        in.read(reinterpret_cast<char*>(&doc_info.term_count), sizeof(doc_info.term_count));
        
        size_t terms_count;
        in.read(reinterpret_cast<char*>(&terms_count), sizeof(terms_count));
        
        for (size_t j = 0; j < terms_count; ++j) {
            size_t term_len;
            in.read(reinterpret_cast<char*>(&term_len), sizeof(term_len));
            std::string term(term_len, '\0');
            in.read(&term[0], term_len);
            doc_info.terms.insert(term);
        }
        
        document_info_[doc_id] = std::move(doc_info);
    }
    
    update_stats();
}

void FullTextIndex::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    inverted_index_.clear();
    document_info_.clear();
    term_frequencies_.clear();
    total_terms_ = 0;
    stats_dirty_ = true;
}

void FullTextIndex::update_stats() const {
    if (!stats_dirty_) return;
    
    total_terms_ = 0;
    for (const auto& pair : document_info_) {
        total_terms_ += pair.second.term_count;
    }
    stats_dirty_ = false;
}

double FullTextIndex::calculate_tf_idf(const std::string& term, const std::string& document_id) const {
    double tf = term_frequency(term, document_id);
    double idf = inverse_document_frequency(term);
    return tf * idf;
}

double FullTextIndex::calculate_document_score(const std::string& document_id, 
                                              const std::vector<std::string>& query_terms) const {
    double score = 0.0;
    
    for (const std::string& term : query_terms) {
        score += calculate_tf_idf(term, document_id);
    }
    
    return score;
}

std::vector<std::string> FullTextIndex::parse_boolean_query(const std::string& query) {
    // 简化实现，实际应该解析 AND, OR, NOT 操作符
    return tokenizer_.tokenize(query);
}

bool FullTextIndex::match_wildcard(const std::string& text, const std::string& pattern) {
    // 简单的通配符匹配，支持 * 和 ?
    try {
        std::string regex_pattern = pattern;
        std::replace(regex_pattern.begin(), regex_pattern.end(), '*', '.');
        regex_pattern = std::regex_replace(regex_pattern, std::regex("\\."), ".*");
        regex_pattern = std::regex_replace(regex_pattern, std::regex("\\?"), ".");
        
        std::regex regex(regex_pattern);
        return std::regex_match(text, regex);
    } catch (const std::exception&) {
        return false;
    }
}

double FullTextIndex::term_frequency(const std::string& term, const std::string& document_id) const {
    auto doc_it = document_info_.find(document_id);
    if (doc_it == document_info_.end()) return 0.0;
    
    if (doc_it->second.terms.count(term) == 0) return 0.0;
    
    // 简化的TF计算：1 / 文档长度
    return 1.0 / doc_it->second.term_count;
}

double FullTextIndex::inverse_document_frequency(const std::string& term) const {
    auto it = inverted_index_.find(term);
    if (it == inverted_index_.end()) return 0.0;
    
    size_t doc_count = document_info_.size();
    size_t term_doc_count = it->second.size();
    
    if (term_doc_count == 0) return 0.0;
    
    return std::log(static_cast<double>(doc_count) / term_doc_count);
}