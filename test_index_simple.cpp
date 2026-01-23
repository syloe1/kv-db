#include "src/index/secondary_index.h"
#include "src/index/composite_index.h"
#include "src/index/tokenizer.h"
#include "src/index/fulltext_index.h"
#include "src/index/inverted_index.h"
#include <iostream>
#include <vector>
#include <string>

void test_secondary_index() {
    std::cout << "\n=== 测试二级索引 ===\n";
    
    SecondaryIndex index("test_index", "value", false);
    
    // 插入测试数据
    std::cout << "插入测试数据...\n";
    for (int i = 0; i < 100; ++i) {
        std::string key = "user_" + std::to_string(i);
        std::string value = "age_" + std::to_string(20 + (i % 50));
        index.insert(value, key);
    }
    
    // 测试查询
    std::cout << "测试精确查询...\n";
    std::vector<std::string> result = index.exact_lookup("age_25");
    std::cout << "查询 age_25 的结果: " << result.size() << " 条记录\n";
    
    // 测试范围查询
    std::cout << "测试范围查询...\n";
    std::vector<std::string> range_result = index.range_lookup("age_25", "age_30");
    std::cout << "范围查询 age_25 到 age_30 的结果: " << range_result.size() << " 条记录\n";
    
    // 显示统计信息
    std::cout << "索引统计:\n";
    std::cout << "  总条目数: " << index.size() << "\n";
    std::cout << "  唯一值数: " << index.unique_values() << "\n";
    std::cout << "  选择性: " << index.selectivity() << "\n";
    std::cout << "  内存使用: " << index.memory_usage() << " bytes\n";
}

void test_composite_index() {
    std::cout << "\n=== 测试复合索引 ===\n";
    
    std::vector<std::string> fields = {"category", "price"};
    CompositeIndex index("product_index", fields);
    
    // 插入测试数据
    std::cout << "插入测试数据...\n";
    for (int i = 0; i < 50; ++i) {
        std::string key = "product_" + std::to_string(i);
        std::vector<std::string> values = {
            "category_" + std::to_string(i % 5),
            "price_" + std::to_string(100 + (i % 100))
        };
        index.insert(values, key);
    }
    
    // 测试精确查询
    std::cout << "测试复合查询...\n";
    std::vector<std::string> search_values = {"category_1", "price_150"};
    std::vector<std::string> result = index.exact_lookup(search_values);
    std::cout << "复合查询结果: " << result.size() << " 条记录\n";
    
    // 测试部分匹配
    std::cout << "测试部分匹配...\n";
    std::vector<std::string> partial_values = {"category_1"};
    std::vector<std::string> partial_result = index.prefix_lookup(partial_values);
    std::cout << "部分匹配查询结果: " << partial_result.size() << " 条记录\n";
    
    std::cout << "复合索引统计:\n";
    std::cout << "  总条目数: " << index.size() << "\n";
    std::cout << "  唯一组合数: " << index.unique_combinations() << "\n";
    std::cout << "  选择性: " << index.selectivity() << "\n";
}

void test_tokenizer() {
    std::cout << "\n=== 测试分词器 ===\n";
    
    Tokenizer tokenizer;
    
    std::string text = "The quick brown fox jumps over the lazy dog";
    std::cout << "原始文本: " << text << "\n";
    
    std::vector<std::string> tokens = tokenizer.tokenize(text);
    std::cout << "分词结果 (" << tokens.size() << " 个词):\n";
    for (const std::string& token : tokens) {
        std::cout << "  " << token << "\n";
    }
    
    // 测试标准化
    std::cout << "标准化测试:\n";
    std::cout << "  'Hello' -> '" << tokenizer.normalize("Hello") << "'\n";
    std::cout << "  'WORLD' -> '" << tokenizer.normalize("WORLD") << "'\n";
    
    // 测试停用词
    std::cout << "停用词测试:\n";
    std::cout << "  'the' 是停用词: " << (tokenizer.is_stop_word("the") ? "是" : "否") << "\n";
    std::cout << "  'fox' 是停用词: " << (tokenizer.is_stop_word("fox") ? "是" : "否") << "\n";
}

void test_fulltext_index() {
    std::cout << "\n=== 测试全文索引 ===\n";
    
    FullTextIndex index("content_index", "value");
    
    // 插入测试文档
    std::cout << "插入测试文档...\n";
    std::vector<std::string> documents = {
        "The quick brown fox jumps over the lazy dog",
        "A fast brown fox leaps over a sleeping dog",
        "The lazy dog sleeps under the tree",
        "Quick brown animals are very fast",
        "Dogs and foxes are common animals"
    };
    
    for (size_t i = 0; i < documents.size(); ++i) {
        std::string doc_id = "doc_" + std::to_string(i);
        index.index_document(doc_id, documents[i]);
    }
    
    // 测试搜索
    std::cout << "测试全文搜索...\n";
    std::vector<std::string> result = index.search("brown fox");
    std::cout << "搜索 'brown fox' 的结果: " << result.size() << " 条记录\n";
    
    // 测试短语搜索
    std::cout << "测试短语搜索...\n";
    std::vector<std::string> terms = {"quick", "brown"};
    std::vector<std::string> phrase_result = index.phrase_search(terms);
    std::cout << "短语搜索 'quick brown' 的结果: " << phrase_result.size() << " 条记录\n";
    
    std::cout << "全文索引统计:\n";
    std::cout << "  文档数: " << index.document_count() << "\n";
    std::cout << "  词条数: " << index.term_count() << "\n";
    std::cout << "  总词数: " << index.total_terms() << "\n";
    std::cout << "  平均文档长度: " << index.average_document_length() << "\n";
}

void test_inverted_index() {
    std::cout << "\n=== 测试倒排索引 ===\n";
    
    InvertedIndex index("inverted_index", "value");
    
    // 插入测试文档
    std::cout << "插入测试文档...\n";
    std::vector<std::string> documents = {
        "machine learning algorithms are powerful",
        "deep learning neural networks",
        "artificial intelligence and machine learning",
        "natural language processing with neural networks",
        "computer vision using deep learning"
    };
    
    for (size_t i = 0; i < documents.size(); ++i) {
        std::string doc_id = "article_" + std::to_string(i);
        index.add_document(doc_id, documents[i]);
    }
    
    // 测试单词搜索
    std::cout << "测试单词搜索...\n";
    std::vector<std::string> result = index.search_term("learning");
    std::cout << "搜索 'learning' 的结果: " << result.size() << " 条记录\n";
    
    // 测试AND搜索
    std::cout << "测试AND搜索...\n";
    std::vector<std::string> terms = {"machine", "learning"};
    std::vector<std::string> and_result = index.search_terms_and(terms);
    std::cout << "AND搜索 'machine' AND 'learning' 的结果: " << and_result.size() << " 条记录\n";
    
    // 测试OR搜索
    std::cout << "测试OR搜索...\n";
    std::vector<std::string> or_result = index.search_terms_or(terms);
    std::cout << "OR搜索 'machine' OR 'learning' 的结果: " << or_result.size() << " 条记录\n";
    
    std::cout << "倒排索引统计:\n";
    std::cout << "  文档数: " << index.document_count() << "\n";
    std::cout << "  词条数: " << index.term_count() << "\n";
    std::cout << "  总倒排项数: " << index.total_postings() << "\n";
    std::cout << "  平均文档长度: " << index.average_document_length() << "\n";
}

int main() {
    std::cout << "KVDB 索引系统简化测试\n";
    std::cout << "======================\n";
    
    try {
        test_secondary_index();
        test_composite_index();
        test_tokenizer();
        test_fulltext_index();
        test_inverted_index();
        
        std::cout << "\n所有测试完成！\n";
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}