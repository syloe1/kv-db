#include "tokenizer.h"
#include <sstream>
#include <cctype>
#include <algorithm>

Tokenizer::Tokenizer() 
    : min_term_length_(2), max_term_length_(50), case_sensitive_(false),
      remove_punctuation_(true), remove_numbers_(false) {
    load_default_stop_words();
}

Tokenizer::~Tokenizer() = default;

std::vector<std::string> Tokenizer::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    
    // 分割文本
    std::vector<std::string> raw_tokens = split_text(text);
    
    for (const std::string& token : raw_tokens) {
        // 清理和标准化
        std::string cleaned = clean_term(token);
        std::string normalized = normalize(cleaned);
        
        // 过滤条件
        if (normalized.empty()) continue;
        if (normalized.length() < min_term_length_) continue;
        if (normalized.length() > max_term_length_) continue;
        if (is_stop_word(normalized)) continue;
        if (remove_numbers_ && is_number(normalized)) continue;
        
        tokens.push_back(normalized);
    }
    
    return tokens;
}

std::string Tokenizer::normalize(const std::string& term) const {
    std::string result = term;
    
    if (!case_sensitive_) {
        result = to_lower(result);
    }
    
    return result;
}

void Tokenizer::add_stop_word(const std::string& word) {
    stop_words_.insert(normalize(word));
}

void Tokenizer::remove_stop_word(const std::string& word) {
    stop_words_.erase(normalize(word));
}

bool Tokenizer::is_stop_word(const std::string& word) const {
    return stop_words_.count(normalize(word)) > 0;
}

void Tokenizer::load_default_stop_words() {
    // 英文常见停用词
    std::vector<std::string> default_stops = {
        "a", "an", "and", "are", "as", "at", "be", "by", "for", "from",
        "has", "he", "in", "is", "it", "its", "of", "on", "that", "the",
        "to", "was", "will", "with", "the", "this", "but", "they", "have",
        "had", "what", "said", "each", "which", "she", "do", "how", "their",
        "if", "up", "out", "many", "then", "them", "these", "so", "some",
        "her", "would", "make", "like", "into", "him", "time", "two", "more",
        "go", "no", "way", "could", "my", "than", "first", "been", "call",
        "who", "oil", "sit", "now", "find", "down", "day", "did", "get",
        "come", "made", "may", "part"
    };
    
    for (const std::string& word : default_stops) {
        stop_words_.insert(word);
    }
}

std::string Tokenizer::to_lower(const std::string& str) const {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

bool Tokenizer::is_punctuation(char c) {
    return std::ispunct(c);
}

bool Tokenizer::is_number(const std::string& str) {
    if (str.empty()) return false;
    
    for (char c : str) {
        if (!std::isdigit(c) && c != '.' && c != '-' && c != '+') {
            return false;
        }
    }
    return true;
}

std::vector<std::string> Tokenizer::split_text(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string token;
    
    // 使用空白字符分割
    while (iss >> token) {
        tokens.push_back(token);
    }
    
    return tokens;
}

std::string Tokenizer::clean_term(const std::string& term) {
    if (!remove_punctuation_) {
        return term;
    }
    
    std::string result;
    result.reserve(term.size());
    
    for (char c : term) {
        if (!is_punctuation(c)) {
            result += c;
        }
    }
    
    return result;
}