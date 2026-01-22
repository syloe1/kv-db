#pragma once
#include "iterator/iterator.h"
#include <vector>
#include <memory>
#include <queue>

// 堆节点，用于优先队列
struct HeapNode {
    int iterator_id;
    std::string key;
    
    HeapNode(int id, const std::string& k) : iterator_id(id), key(k) {}
    
    // 最小堆比较器（key 小的优先级高）
    bool operator>(const HeapNode& other) const {
        return key > other.key;
    }
};

class MergeIterator : public Iterator {
public:
    MergeIterator(std::vector<std::unique_ptr<Iterator>> children);

    void seek(const std::string& target) override;
    void seek_to_first();
    void seek_with_prefix(const std::string& prefix);
    void next() override;
    bool valid() const override;

    std::string key() const override;
    std::string value() const override;

private:
    void init_heap(); // 初始化堆
    void advance_iterator(int id); // 推进指定迭代器并更新堆
    void skip_tombstones(); // 跳过墓碑记录

    std::vector<std::unique_ptr<Iterator>> children_;
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> min_heap_;
    
    // 当前状态
    bool is_valid_;
    std::string current_key_;
    std::string current_value_;
    
    // Prefix 优化相关
    std::string prefix_filter_;
    bool use_prefix_filter_;
};
