#include "inverted_index.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <set>
#include <mutex>
#include <shared_mutex>

InvertedIndex::InvertedIndex(const std::string& name, const std::string& field)
    : name_(name), field_(field), total_documents_(0), total_postings_(0), stats_dirty_(true) {
}

InvertedIndex::~InvertedIndex() = default;

void InvertedIndex::add_document(const std::string& document_id, const std::string& text) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 如果文档已存在，先移除（内部调用，不加锁）
    remove_document_internal(document_id);
    
    // 分词并获取位置信息
    std::vector<uint32_t> positions;
    std::vector<std::string> terms = tokenize_with_positions(text, positions);
    
    if (terms.empty()) {
        return;
    }
    
    // 统计词频
    std::map<std::string, uint32_t> term_frequencies;
    std::map<std::string, std::vector<uint32_t>> term_positions;
    
    for (size_t i = 0; i < terms.size(); ++i) {
        const std::string& term = terms[i];
        term_frequencies[term]++;
        term_positions[term].push_back(positions[i]);
    }
    
    // 更新倒排索引
    for (const auto& pair : term_frequencies) {
        const std::string& term = pair.first;
        uint32_t frequency = pair.second;
        
        // 获取或创建倒排列表
        if (index_.find(term) == index_.end()) {
            index_[term] = std::make_unique<PostingList>(term);
        }
        
        PostingList* posting_list = index_[term].get();
        
        // 创建文档词条
        DocumentTerm doc_term(document_id);
        doc_term.frequency = frequency;
        
        // 添加位置信息
        for (uint32_t pos : term_positions[term]) {
            doc_term.positions.emplace_back(pos);
        }
        
        posting_list->documents.push_back(std::move(doc_term));
        posting_list->document_frequency++;
    }
    
    // 记录文档长度
    document_lengths_[document_id] = static_cast<uint32_t>(terms.size());
    stats_dirty_ = true;
}
void InvertedIndex::remove_document(const std::string& document_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    remove_document_internal(document_id);
}

void InvertedIndex::remove_document_internal(const std::string& document_id) {
    // 从所有倒排列表中移除该文档
    for (auto it = index_.begin(); it != index_.end();) {
        PostingList* posting_list = it->second.get();
        
        // 查找并移除文档
        auto doc_it = std::find_if(posting_list->documents.begin(), 
                                  posting_list->documents.end(),
                                  [&document_id](const DocumentTerm& doc_term) {
                                      return doc_term.document_id == document_id;
                                  });
        
        if (doc_it != posting_list->documents.end()) {
            posting_list->documents.erase(doc_it);
            posting_list->document_frequency--;
            
            // 如果倒排列表为空，删除该词条
            if (posting_list->documents.empty()) {
                it = index_.erase(it);
                continue;
            }
        }
        
        ++it;
    }
    
    // 移除文档长度记录
    document_lengths_.erase(document_id);
    stats_dirty_ = true;
}

void InvertedIndex::update_document(const std::string& document_id, 
                                   const std::string& old_text, 
                                   const std::string& new_text) {
    if (old_text == new_text) {
        return;
    }
    
    // 重新添加文档
    add_document(document_id, new_text);
}

std::vector<std::string> InvertedIndex::search_term(const std::string& term) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::string normalized_term = tokenizer_.normalize(term);
    auto it = index_.find(normalized_term);
    if (it == index_.end()) {
        return {};
    }
    
    std::vector<std::string> result;
    const PostingList* posting_list = it->second.get();
    
    for (const DocumentTerm& doc_term : posting_list->documents) {
        result.push_back(doc_term.document_id);
    }
    
    return result;
}

std::vector<std::string> InvertedIndex::search_terms_and(const std::vector<std::string>& terms) {
    if (terms.empty()) {
        return {};
    }
    
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::vector<std::string>> document_lists;
    
    for (const std::string& term : terms) {
        std::string normalized_term = tokenizer_.normalize(term);
        auto it = index_.find(normalized_term);
        if (it == index_.end()) {
            return {}; // 如果任何词不存在，返回空结果
        }
        
        std::vector<std::string> doc_list;
        const PostingList* posting_list = it->second.get();
        
        for (const DocumentTerm& doc_term : posting_list->documents) {
            doc_list.push_back(doc_term.document_id);
        }
        
        document_lists.push_back(std::move(doc_list));
    }
    
    return intersect_document_lists(document_lists);
}

std::vector<std::string> InvertedIndex::search_terms_or(const std::vector<std::string>& terms) {
    if (terms.empty()) {
        return {};
    }
    
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::vector<std::string>> document_lists;
    
    for (const std::string& term : terms) {
        std::string normalized_term = tokenizer_.normalize(term);
        auto it = index_.find(normalized_term);
        if (it != index_.end()) {
            std::vector<std::string> doc_list;
            const PostingList* posting_list = it->second.get();
            
            for (const DocumentTerm& doc_term : posting_list->documents) {
                doc_list.push_back(doc_term.document_id);
            }
            
            document_lists.push_back(std::move(doc_list));
        }
    }
    
    return union_document_lists(document_lists);
}
std::vector<std::string> InvertedIndex::phrase_search(const std::vector<std::string>& terms, uint32_t max_distance) {
    if (terms.empty()) {
        return {};
    }
    
    // 先进行AND搜索获取候选文档
    std::vector<std::string> candidates = search_terms_and(terms);
    
    std::vector<std::string> result;
    
    // 检查每个候选文档是否满足短语条件
    for (const std::string& doc_id : candidates) {
        if (check_phrase_match(terms, doc_id, max_distance)) {
            result.push_back(doc_id);
        }
    }
    
    return result;
}

std::vector<std::string> InvertedIndex::proximity_search(const std::vector<std::string>& terms, uint32_t max_distance) {
    // 邻近搜索与短语搜索类似，但允许更大的距离
    return phrase_search(terms, max_distance);
}

std::vector<std::string> InvertedIndex::boolean_search(const std::string& query) {
    // 简化的布尔搜索实现
    // 实际应该解析复杂的布尔表达式
    std::vector<std::string> terms = tokenizer_.tokenize(query);
    return search_terms_and(terms);
}

std::vector<InvertedIndex::SearchResult> InvertedIndex::ranked_search(const std::vector<std::string>& terms, size_t limit) {
    if (terms.empty()) {
        return {};
    }
    
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    // 收集所有候选文档
    std::set<std::string> candidate_docs;
    for (const std::string& term : terms) {
        std::string normalized_term = tokenizer_.normalize(term);
        auto it = index_.find(normalized_term);
        if (it != index_.end()) {
            const PostingList* posting_list = it->second.get();
            for (const DocumentTerm& doc_term : posting_list->documents) {
                candidate_docs.insert(doc_term.document_id);
            }
        }
    }
    
    // 计算每个文档的得分
    std::vector<SearchResult> results;
    for (const std::string& doc_id : candidate_docs) {
        SearchResult result(doc_id);
        result.score = calculate_document_score(terms, doc_id);
        
        if (result.score > 0) {
            // 收集匹配的词条和位置
            for (const std::string& term : terms) {
                std::string normalized_term = tokenizer_.normalize(term);
                auto it = index_.find(normalized_term);
                if (it != index_.end()) {
                    const PostingList* posting_list = it->second.get();
                    
                    for (const DocumentTerm& doc_term : posting_list->documents) {
                        if (doc_term.document_id == doc_id) {
                            result.matched_terms.push_back(term);
                            result.match_positions.insert(result.match_positions.end(),
                                                         doc_term.positions.begin(),
                                                         doc_term.positions.end());
                            break;
                        }
                    }
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

const InvertedIndex::PostingList* InvertedIndex::get_posting_list(const std::string& term) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::string normalized_term = tokenizer_.normalize(term);
    auto it = index_.find(normalized_term);
    if (it != index_.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<std::string> InvertedIndex::get_all_terms() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> terms;
    terms.reserve(index_.size());
    
    for (const auto& pair : index_) {
        terms.push_back(pair.first);
    }
    
    return terms;
}
size_t InvertedIndex::document_count() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return document_lengths_.size();
}

size_t InvertedIndex::term_count() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return index_.size();
}

size_t InvertedIndex::total_postings() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (stats_dirty_) {
        update_stats();
    }
    return total_postings_;
}

double InvertedIndex::average_document_length() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    if (document_lengths_.empty()) {
        return 0.0;
    }
    
    uint64_t total_length = 0;
    for (const auto& pair : document_lengths_) {
        total_length += pair.second;
    }
    
    return static_cast<double>(total_length) / document_lengths_.size();
}

size_t InvertedIndex::memory_usage() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    size_t usage = sizeof(*this);
    
    // 倒排索引
    for (const auto& pair : index_) {
        usage += pair.first.size();
        usage += sizeof(PostingList);
        
        const PostingList* posting_list = pair.second.get();
        for (const DocumentTerm& doc_term : posting_list->documents) {
            usage += doc_term.document_id.size();
            usage += sizeof(DocumentTerm);
            usage += doc_term.positions.size() * sizeof(PositionInfo);
        }
    }
    
    // 文档长度
    for (const auto& pair : document_lengths_) {
        usage += pair.first.size();
        usage += sizeof(uint32_t);
    }
    
    return usage;
}

void InvertedIndex::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    index_.clear();
    document_lengths_.clear();
    total_documents_ = 0;
    total_postings_ = 0;
    stats_dirty_ = true;
}

void InvertedIndex::update_stats() const {
    if (!stats_dirty_) return;
    
    total_documents_ = document_lengths_.size();
    total_postings_ = 0;
    
    for (const auto& pair : index_) {
        total_postings_ += pair.second->documents.size();
    }
    
    stats_dirty_ = false;
}

std::vector<std::string> InvertedIndex::tokenize_with_positions(const std::string& text, 
                                                               std::vector<uint32_t>& positions) {
    std::vector<std::string> terms = tokenizer_.tokenize(text);
    positions.clear();
    positions.reserve(terms.size());
    
    // 简化的位置计算，实际应该基于原始文本位置
    for (uint32_t i = 0; i < terms.size(); ++i) {
        positions.push_back(i);
    }
    
    return terms;
}

std::vector<std::string> InvertedIndex::intersect_document_lists(const std::vector<std::vector<std::string>>& lists) {
    if (lists.empty()) {
        return {};
    }
    
    if (lists.size() == 1) {
        return lists[0];
    }
    
    std::set<std::string> result_set(lists[0].begin(), lists[0].end());
    
    for (size_t i = 1; i < lists.size(); ++i) {
        std::set<std::string> current_set(lists[i].begin(), lists[i].end());
        std::set<std::string> intersection;
        
        std::set_intersection(result_set.begin(), result_set.end(),
                            current_set.begin(), current_set.end(),
                            std::inserter(intersection, intersection.begin()));
        
        result_set = std::move(intersection);
        
        if (result_set.empty()) {
            break;
        }
    }
    
    return std::vector<std::string>(result_set.begin(), result_set.end());
}

std::vector<std::string> InvertedIndex::union_document_lists(const std::vector<std::vector<std::string>>& lists) {
    std::set<std::string> result_set;
    
    for (const auto& list : lists) {
        result_set.insert(list.begin(), list.end());
    }
    
    return std::vector<std::string>(result_set.begin(), result_set.end());
}
bool InvertedIndex::check_phrase_match(const std::vector<std::string>& terms, 
                                       const std::string& document_id, 
                                       uint32_t max_distance) {
    if (terms.empty()) {
        return false;
    }
    
    // 获取每个词在文档中的位置
    std::vector<std::vector<uint32_t>> term_positions(terms.size());
    
    for (size_t i = 0; i < terms.size(); ++i) {
        std::string normalized_term = tokenizer_.normalize(terms[i]);
        auto it = index_.find(normalized_term);
        if (it == index_.end()) {
            return false;
        }
        
        const PostingList* posting_list = it->second.get();
        
        // 查找文档
        for (const DocumentTerm& doc_term : posting_list->documents) {
            if (doc_term.document_id == document_id) {
                for (const PositionInfo& pos_info : doc_term.positions) {
                    term_positions[i].push_back(pos_info.position);
                }
                break;
            }
        }
        
        if (term_positions[i].empty()) {
            return false;
        }
    }
    
    // 检查是否存在满足距离要求的位置组合
    return check_position_sequence(term_positions, max_distance);
}

double InvertedIndex::calculate_bm25_score(const std::string& term, const std::string& document_id) const {
    auto term_it = index_.find(term);
    if (term_it == index_.end()) {
        return 0.0;
    }
    
    const PostingList* posting_list = term_it->second.get();
    
    // 查找文档中的词频
    uint32_t tf = 0;
    for (const DocumentTerm& doc_term : posting_list->documents) {
        if (doc_term.document_id == document_id) {
            tf = doc_term.frequency;
            break;
        }
    }
    
    if (tf == 0) {
        return 0.0;
    }
    
    // 获取文档长度
    auto doc_it = document_lengths_.find(document_id);
    if (doc_it == document_lengths_.end()) {
        return 0.0;
    }
    
    uint32_t doc_length = doc_it->second;
    double avg_doc_length = average_document_length();
    
    // 计算IDF
    size_t total_docs = document_lengths_.size();
    size_t docs_with_term = posting_list->document_frequency;
    double idf = std::log((total_docs - docs_with_term + 0.5) / (docs_with_term + 0.5));
    
    // 计算BM25得分
    double tf_component = (tf * (K1 + 1)) / (tf + K1 * (1 - B + B * (doc_length / avg_doc_length)));
    
    return idf * tf_component;
}

double InvertedIndex::calculate_document_score(const std::vector<std::string>& terms, const std::string& document_id) const {
    double score = 0.0;
    
    for (const std::string& term : terms) {
        std::string normalized_term = tokenizer_.normalize(term);
        score += calculate_bm25_score(normalized_term, document_id);
    }
    
    return score;
}

// 序列化和反序列化方法
void InvertedIndex::serialize(std::ostream& out) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    // 写入基本信息
    size_t name_len = name_.size();
    out.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
    out.write(name_.c_str(), name_len);
    
    size_t field_len = field_.size();
    out.write(reinterpret_cast<const char*>(&field_len), sizeof(field_len));
    out.write(field_.c_str(), field_len);
    
    // 写入倒排索引
    size_t index_size = index_.size();
    out.write(reinterpret_cast<const char*>(&index_size), sizeof(index_size));
    
    for (const auto& pair : index_) {
        const std::string& term = pair.first;
        const PostingList* posting_list = pair.second.get();
        
        // 写入词条
        size_t term_len = term.size();
        out.write(reinterpret_cast<const char*>(&term_len), sizeof(term_len));
        out.write(term.c_str(), term_len);
        
        // 写入文档频率
        out.write(reinterpret_cast<const char*>(&posting_list->document_frequency), sizeof(posting_list->document_frequency));
        
        // 写入文档列表
        size_t doc_count = posting_list->documents.size();
        out.write(reinterpret_cast<const char*>(&doc_count), sizeof(doc_count));
        
        for (const DocumentTerm& doc_term : posting_list->documents) {
            // 写入文档ID
            size_t doc_id_len = doc_term.document_id.size();
            out.write(reinterpret_cast<const char*>(&doc_id_len), sizeof(doc_id_len));
            out.write(doc_term.document_id.c_str(), doc_id_len);
            
            // 写入词频
            out.write(reinterpret_cast<const char*>(&doc_term.frequency), sizeof(doc_term.frequency));
            
            // 写入位置信息
            size_t pos_count = doc_term.positions.size();
            out.write(reinterpret_cast<const char*>(&pos_count), sizeof(pos_count));
            
            for (const PositionInfo& pos_info : doc_term.positions) {
                out.write(reinterpret_cast<const char*>(&pos_info.position), sizeof(pos_info.position));
                out.write(reinterpret_cast<const char*>(&pos_info.sentence_id), sizeof(pos_info.sentence_id));
                out.write(reinterpret_cast<const char*>(&pos_info.paragraph_id), sizeof(pos_info.paragraph_id));
            }
        }
    }
    
    // 写入文档长度信息
    size_t doc_len_count = document_lengths_.size();
    out.write(reinterpret_cast<const char*>(&doc_len_count), sizeof(doc_len_count));
    
    for (const auto& pair : document_lengths_) {
        size_t doc_id_len = pair.first.size();
        out.write(reinterpret_cast<const char*>(&doc_id_len), sizeof(doc_id_len));
        out.write(pair.first.c_str(), doc_id_len);
        out.write(reinterpret_cast<const char*>(&pair.second), sizeof(pair.second));
    }
}

// 添加缺失的辅助方法
bool InvertedIndex::check_position_sequence(const std::vector<std::vector<uint32_t>>& term_positions, 
                                           uint32_t max_distance) const {
    if (term_positions.empty()) {
        return false;
    }
    
    // 简化实现：检查是否存在连续的位置序列
    // 实际实现应该更复杂，考虑所有可能的位置组合
    
    for (uint32_t pos : term_positions[0]) {
        bool sequence_found = true;
        uint32_t last_pos = pos;
        
        for (size_t i = 1; i < term_positions.size(); ++i) {
            bool found_next = false;
            
            for (uint32_t next_pos : term_positions[i]) {
                if (next_pos > last_pos && (next_pos - last_pos) <= max_distance + 1) {
                    last_pos = next_pos;
                    found_next = true;
                    break;
                }
            }
            
            if (!found_next) {
                sequence_found = false;
                break;
            }
        }
        
        if (sequence_found) {
            return true;
        }
    }
    
    return false;
}
void InvertedIndex::deserialize(std::istream& in) {
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
    index_.clear();
    size_t index_size;
    in.read(reinterpret_cast<char*>(&index_size), sizeof(index_size));
    
    for (size_t i = 0; i < index_size; ++i) {
        // 读取词条
        size_t term_len;
        in.read(reinterpret_cast<char*>(&term_len), sizeof(term_len));
        std::string term(term_len, '\0');
        in.read(&term[0], term_len);
        
        auto posting_list = std::make_unique<PostingList>(term);
        
        // 读取文档频率
        in.read(reinterpret_cast<char*>(&posting_list->document_frequency), sizeof(posting_list->document_frequency));
        
        // 读取文档列表
        size_t doc_count;
        in.read(reinterpret_cast<char*>(&doc_count), sizeof(doc_count));
        
        for (size_t j = 0; j < doc_count; ++j) {
            // 读取文档ID
            size_t doc_id_len;
            in.read(reinterpret_cast<char*>(&doc_id_len), sizeof(doc_id_len));
            std::string doc_id(doc_id_len, '\0');
            in.read(&doc_id[0], doc_id_len);
            
            DocumentTerm doc_term(doc_id);
            
            // 读取词频
            in.read(reinterpret_cast<char*>(&doc_term.frequency), sizeof(doc_term.frequency));
            
            // 读取位置信息
            size_t pos_count;
            in.read(reinterpret_cast<char*>(&pos_count), sizeof(pos_count));
            
            for (size_t k = 0; k < pos_count; ++k) {
                PositionInfo pos_info(0);
                in.read(reinterpret_cast<char*>(&pos_info.position), sizeof(pos_info.position));
                in.read(reinterpret_cast<char*>(&pos_info.sentence_id), sizeof(pos_info.sentence_id));
                in.read(reinterpret_cast<char*>(&pos_info.paragraph_id), sizeof(pos_info.paragraph_id));
                doc_term.positions.push_back(pos_info);
            }
            
            posting_list->documents.push_back(std::move(doc_term));
        }
        
        index_[term] = std::move(posting_list);
    }
    
    // 读取文档长度信息
    document_lengths_.clear();
    size_t doc_len_count;
    in.read(reinterpret_cast<char*>(&doc_len_count), sizeof(doc_len_count));
    
    for (size_t i = 0; i < doc_len_count; ++i) {
        size_t doc_id_len;
        in.read(reinterpret_cast<char*>(&doc_id_len), sizeof(doc_id_len));
        std::string doc_id(doc_id_len, '\0');
        in.read(&doc_id[0], doc_id_len);
        
        uint32_t length;
        in.read(reinterpret_cast<char*>(&length), sizeof(length));
        document_lengths_[doc_id] = length;
    }
    
    update_stats();
}