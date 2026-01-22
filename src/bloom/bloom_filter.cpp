#include "bloom/bloom_filter.h"
#include <functional>

BloomFilter::BloomFilter(size_t bits, size_t hashes)
    : bit_size_(bits), hash_count_(hashes), bits_(bits, false) {}

size_t BloomFilter::hash(const std::string& key, size_t seed) const {
    size_t h = std::hash<std::string>{}(key);
    return (h + seed * 0x9e3779b9) % bit_size_;
}

void BloomFilter::add(const std::string& key) {
    for (size_t i = 0; i < hash_count_; ++i) {
        bits_[hash(key, i)] = true;
    }
}

bool BloomFilter::possiblyContains(const std::string& key) const {
    for (size_t i = 0; i < hash_count_; ++i) {
        if (!bits_[hash(key, i)]) return false;
    }
    return true;
}

void BloomFilter::serialize(std::ostream& out) const {
    for (bool bit : bits_) {
        out << (bit ? '1' : '0');
    }
    out << "\n";
}

void BloomFilter::deserialize(std::istream& in) {
    std::string line;
    std::getline(in, line);
    for (size_t i = 0; i < bit_size_ && i < line.size(); ++i) {
        bits_[i] = (line[i] == '1');
    }
}