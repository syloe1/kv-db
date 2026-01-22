#pragma once
#include <vector>
#include <string>
#include <iostream>

class BloomFilter {
public:
    BloomFilter(size_t bits = 8192, size_t hashes = 3);

    void add(const std::string& key);
    bool possiblyContains(const std::string& key) const;

    void serialize(std::ostream& out) const;
    void deserialize(std::istream& in);

private:
    size_t bit_size_;
    size_t hash_count_;
    std::vector<bool> bits_;

    size_t hash(const std::string& key, size_t seed) const;
};