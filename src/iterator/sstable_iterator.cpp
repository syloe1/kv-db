#include "iterator/sstable_iterator.h"
#include "sstable/sstable_reader.h"
#include "bloom/bloom_filter.h"
#include <sstream>
#include <algorithm>

static const std::string TOMBSTONE = "__TOMBSTONE__";

static SSTableFooter read_footer(std::ifstream& in) {
    in.seekg(0, std::ios::end);
    std::streampos file_size = in.tellg();

    std::string last_line;
    long pos = (long)file_size - 1;

    in.seekg(pos);
    char c;
    while (pos > 0 && in.get(c) && c == '\n') {
        pos--;
        in.seekg(pos);
    }

    while (pos > 0) {
        in.seekg(pos);
        in.get(c);
        if (c == '\n') {
            pos++;
            break;
        }
        pos--;
    }
    
    in.clear();
    in.seekg(pos);
    std::getline(in, last_line);

    SSTableFooter footer = {0, 0};
    std::istringstream iss(last_line);
    iss >> footer.index_offset >> footer.bloom_offset;

    return footer;
}

SSTableIterator::SSTableIterator(const SSTableMeta& meta, uint64_t snapshot_seq)
    : meta_(meta), snapshot_seq_(snapshot_seq),
      current_index_pos_(-1), current_version_pos_(-1),
      is_valid_(false), use_prefix_filter_(false) {
    file_.open(meta_.filename);
    if (file_.is_open()) {
        load_index();
        if (!index_.empty()) {
            seek_to_first(); // 定位到第一个 key
        }
    }
}

SSTableIterator::~SSTableIterator() {
    if (file_.is_open()) {
        file_.close();
    }
}

void SSTableIterator::load_index() {
    SSTableFooter footer = read_footer(file_);
    
    file_.clear();
    file_.seekg(footer.index_offset);
    
    std::string line;
    while (std::getline(file_, line)) {
        // 检查是否是 footer 行（包含 bloom filter 的二进制数据）
        if (!line.empty() && (line[0] == '0' || line[0] == '1') && line.size() > 100) {
            break;
        }
        
        std::istringstream iss(line);
        std::string key;
        uint64_t offset;
        if (iss >> key >> offset) {
            index_.emplace_back(key, offset);
        } else {
            break;
        }
    }
}

void SSTableIterator::load_data_block() {
    if (current_index_pos_ < 0 || current_index_pos_ >= (int)index_.size()) {
        return;
    }
    
    current_versions_.clear();
    current_version_pos_ = -1;
    
    uint64_t offset = index_[current_index_pos_].second;
    file_.clear();
    file_.seekg(offset);
    
    std::string line;
    std::string first_key;
    
    // 读取该 key 的所有版本
    while (std::getline(file_, line)) {
        std::istringstream iss(line);
        std::string key;
        uint64_t seq;
        std::string value;
        
        if (!(iss >> key >> seq >> value)) {
            break; // 读到下一个 key 或文件结束
        }
        
        if (first_key.empty()) {
            first_key = key;
        }
        
        if (key != first_key) {
            break; // 读到下一个 key
        }
        
        current_versions_.emplace_back(seq, value);
    }
    
    // 按 seq DESC 排序（最新版本在前）
    std::sort(current_versions_.begin(), current_versions_.end(),
              [](const auto& a, const auto& b) {
                  return a.first > b.first; // DESC
              });
    
    find_next_valid_version();
}

void SSTableIterator::find_next_valid_version() {
    current_value_ = "";
    is_valid_ = false;
    
    // 找到 <= snapshot_seq 的第一个版本
    for (size_t i = 0; i < current_versions_.size(); i++) {
        if (current_versions_[i].first <= snapshot_seq_) {
            if (current_versions_[i].second != TOMBSTONE) {
                current_value_ = current_versions_[i].second;
                current_version_pos_ = i;
                is_valid_ = true;
            }
            break; // 找到可见版本（即使是 Tombstone 也停止）
        }
    }
}

void SSTableIterator::seek(const std::string& target) {
    use_prefix_filter_ = false;
    
    // 在 index 中二分查找
    int l = 0, r = (int)index_.size() - 1;
    int pos = -1;
    
    while (l <= r) {
        int mid = (l + r) / 2;
        if (index_[mid].first >= target) {
            pos = mid;
            r = mid - 1;
        } else {
            l = mid + 1;
        }
    }
    
    if (pos == -1) {
        current_index_pos_ = index_.size(); // 超出范围
        is_valid_ = false;
        return;
    }
    
    current_index_pos_ = pos;
    current_key_ = index_[pos].first;
    load_data_block();
}

void SSTableIterator::seek_to_first() {
    use_prefix_filter_ = false;
    
    if (index_.empty()) {
        is_valid_ = false;
        return;
    }
    
    current_index_pos_ = 0;
    current_key_ = index_[0].first;
    load_data_block();
}

void SSTableIterator::seek_with_prefix(const std::string& prefix) {
    use_prefix_filter_ = true;
    prefix_filter_ = prefix;
    
    // 使用二分查找找到第一个 >= prefix 的 key
    seek(prefix);
    
    // 检查当前 key 是否匹配前缀
    if (valid() && !key_matches_prefix()) {
        is_valid_ = false;
    }
}

bool SSTableIterator::key_matches_prefix() const {
    if (!use_prefix_filter_) return true;
    return current_key_.size() >= prefix_filter_.size() && 
           current_key_.substr(0, prefix_filter_.size()) == prefix_filter_;
}

void SSTableIterator::next() {
    if (!valid()) return;
    
    // 移动到下一个 key
    current_index_pos_++;
    if (current_index_pos_ >= (int)index_.size()) {
        is_valid_ = false;
        return;
    }
    
    current_key_ = index_[current_index_pos_].first;
    
    // Prefix 过滤优化
    if (use_prefix_filter_ && !key_matches_prefix()) {
        is_valid_ = false;
        return;
    }
    
    load_data_block();
}

bool SSTableIterator::valid() const {
    return is_valid_ && current_index_pos_ >= 0 && 
           current_index_pos_ < (int)index_.size();
}

std::string SSTableIterator::key() const {
    if (!valid()) return "";
    return current_key_;
}

std::string SSTableIterator::value() const {
    if (!valid()) return "";
    return current_value_;
}
