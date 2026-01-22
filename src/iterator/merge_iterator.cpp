#include "iterator/merge_iterator.h"
#include <algorithm>

MergeIterator::MergeIterator(std::vector<std::unique_ptr<Iterator>> children)
    : children_(std::move(children)), is_valid_(false), use_prefix_filter_(false) {
    init_heap();
}

void MergeIterator::init_heap() {
    // 清空堆
    while (!min_heap_.empty()) {
        min_heap_.pop();
    }
    
    // 将所有有效的迭代器加入堆
    for (int i = 0; i < (int)children_.size(); i++) {
        if (children_[i]->valid()) {
            min_heap_.emplace(i, children_[i]->key());
        }
    }
    
    // 设置当前状态
    if (!min_heap_.empty()) {
        const HeapNode& top = min_heap_.top();
        current_key_ = top.key;
        
        // 找到相同 key 中最新的非墓碑版本
        std::string best_value;
        bool found_valid = false;
        
        // 收集所有相同 key 的值
        std::vector<int> same_key_iterators;
        while (!min_heap_.empty() && min_heap_.top().key == current_key_) {
            same_key_iterators.push_back(min_heap_.top().iterator_id);
            min_heap_.pop();
        }
        
        // 按迭代器优先级顺序查找最新的非墓碑版本
        for (int id : same_key_iterators) {
            std::string value = children_[id]->value();
            if (!value.empty()) {
                current_value_ = value;
                found_valid = true;
                break; // 找到第一个非空值就停止（最高优先级的）
            }
        }
        
        is_valid_ = found_valid;
        
        // 将这些迭代器推进到下一个位置并重新加入堆
        for (int id : same_key_iterators) {
            advance_iterator(id);
        }
    } else {
        is_valid_ = false;
    }
    
    // 如果当前记录是墓碑，跳过
    if (is_valid_ && current_value_.empty()) {
        skip_tombstones();
    }
}

void MergeIterator::advance_iterator(int id) {
    children_[id]->next();
    if (children_[id]->valid()) {
        std::string next_key = children_[id]->key();
        
        // Prefix 过滤优化
        if (use_prefix_filter_ && !(next_key.size() >= prefix_filter_.size() && 
                                   next_key.substr(0, prefix_filter_.size()) == prefix_filter_)) {
            return; // 不加入堆，因为超出了前缀范围
        }
        
        min_heap_.emplace(id, next_key);
    }
}

void MergeIterator::skip_tombstones() {
    while (!min_heap_.empty()) {
        const HeapNode& top = min_heap_.top();
        current_key_ = top.key;
        
        // 收集所有相同 key 的值
        std::vector<int> same_key_iterators;
        while (!min_heap_.empty() && min_heap_.top().key == current_key_) {
            same_key_iterators.push_back(min_heap_.top().iterator_id);
            min_heap_.pop();
        }
        
        // 查找最新的非墓碑版本
        bool found_valid = false;
        for (int id : same_key_iterators) {
            std::string value = children_[id]->value();
            if (!value.empty()) {
                current_value_ = value;
                found_valid = true;
                break;
            }
        }
        
        // 推进这些迭代器
        for (int id : same_key_iterators) {
            advance_iterator(id);
        }
        
        if (found_valid) {
            is_valid_ = true;
            return;
        }
    }
    
    is_valid_ = false;
}

void MergeIterator::seek(const std::string& target) {
    use_prefix_filter_ = false;
    
    // 所有子迭代器都 seek 到 target
    for (auto& it : children_) {
        it->seek(target);
    }
    
    init_heap();
}

void MergeIterator::seek_to_first() {
    use_prefix_filter_ = false;
    
    // 所有子迭代器都 seek 到开始
    for (auto& it : children_) {
        it->seek("");
    }
    
    init_heap();
}

void MergeIterator::seek_with_prefix(const std::string& prefix) {
    use_prefix_filter_ = true;
    prefix_filter_ = prefix;
    
    // 所有子迭代器都 seek 到 prefix
    for (auto& it : children_) {
        it->seek(prefix);
    }
    
    init_heap();
}

void MergeIterator::next() {
    if (!is_valid_) return;
    
    // 如果堆为空，说明没有更多数据
    if (min_heap_.empty()) {
        is_valid_ = false;
        return;
    }
    
    // 获取下一个最小的 key
    const HeapNode& top = min_heap_.top();
    current_key_ = top.key;
    
    // Prefix 过滤优化
    if (use_prefix_filter_ && !(current_key_.size() >= prefix_filter_.size() && 
                               current_key_.substr(0, prefix_filter_.size()) == prefix_filter_)) {
        is_valid_ = false;
        return;
    }
    
    // 收集所有相同 key 的值
    std::vector<int> same_key_iterators;
    while (!min_heap_.empty() && min_heap_.top().key == current_key_) {
        same_key_iterators.push_back(min_heap_.top().iterator_id);
        min_heap_.pop();
    }
    
    // 查找最新的非墓碑版本
    bool found_valid = false;
    for (int id : same_key_iterators) {
        std::string value = children_[id]->value();
        if (!value.empty()) {
            current_value_ = value;
            found_valid = true;
            break;
        }
    }
    
    // 推进这些迭代器
    for (int id : same_key_iterators) {
        advance_iterator(id);
    }
    
    if (!found_valid) {
        skip_tombstones();
    } else {
        is_valid_ = true;
    }
}

bool MergeIterator::valid() const {
    return is_valid_;
}

std::string MergeIterator::key() const {
    if (!is_valid_) return "";
    return current_key_;
}

std::string MergeIterator::value() const {
    if (!is_valid_) return "";
    return current_value_;
}
