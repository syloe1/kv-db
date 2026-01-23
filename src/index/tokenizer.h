#pragma once
#include <string>
#include <vector>
#include <set>
#include <unordered_set>
#include <locale>
#include <algorithm>

class Tokenizer {
public:
    Tokenizer();
    ~Tokenizer();
    
    // 分词
    std::vector<std::string> tokenize(const std::string& text);
    
    // 标准化
    std::string normalize(const std::string& term) const;
    
    // 停用词管理
    void add_stop_word(const std::string& word);
    void remove_stop_word(const std::string& word);
    bool is_stop_word(const std::string& word) const;
    void load_default_stop_words();
    
    // 配置选项
    void set_min_term_length(size_t length) { min_term_length_ = length; }
    void set_max_term_length(size_t length) { max_term_length_ = length; }
    void set_case_sensitive(bool sensitive) { case_sensitive_ = sensitive; }
    void set_remove_punctuation(bool remove) { remove_punctuation_ = remove; }
    void set_remove_numbers(bool remove) { remove_numbers_ = remove; }
    
    size_t get_min_term_length() const { return min_term_length_; }
    size_t get_max_term_length() const { return max_term_length_; }
    bool is_case_sensitive() const { return case_sensitive_; }
    bool should_remove_punctuation() const { return remove_punctuation_; }
    bool should_remove_numbers() const { return remove_numbers_; }
    
private:
    // 停用词集合
    std::unordered_set<std::string> stop_words_;
    
    // 配置选项
    size_t min_term_length_;
    size_t max_term_length_;
    bool case_sensitive_;
    bool remove_punctuation_;
    bool remove_numbers_;
    
    // 辅助方法
    std::string to_lower(const std::string& str) const;
    bool is_punctuation(char c);
    bool is_number(const std::string& str);
    std::vector<std::string> split_text(const std::string& text);
    std::string clean_term(const std::string& term);
};