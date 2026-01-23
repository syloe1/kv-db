#!/bin/bash

# 故障恢复优化功能综合测试脚本
# Fault Recovery Optimization Comprehensive Test Script

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 测试结果统计
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
    ((PASSED_TESTS++))
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
    ((FAILED_TESTS++))
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# 运行单个测试
run_test() {
    local test_name="$1"
    local test_command="$2"
    
    ((TOTAL_TESTS++))
    log_info "运行测试: $test_name"
    
    if eval "$test_command"; then
        log_success "$test_name 通过"
        return 0
    else
        log_error "$test_name 失败"
        return 1
    fi
}

# 检查构建环境
check_build_environment() {
    log_info "检查构建环境..."
    
    # 检查必要的工具
    if ! command -v cmake &> /dev/null; then
        log_error "CMake 未安装"
        exit 1
    fi
    
    if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
        log_error "C++ 编译器未安装"
        exit 1
    fi
    
    log_success "构建环境检查通过"
}

# 构建项目
build_project() {
    log_info "构建项目..."
    
    # 创建构建目录
    mkdir -p build
    cd build
    
    # 配置和构建
    if cmake .. && make -j$(nproc); then
        log_success "项目构建成功"
        cd ..
        return 0
    else
        log_error "项目构建失败"
        cd ..
        return 1
    fi
}

# 编译测试程序
compile_tests() {
    log_info "编译测试程序..."
    
    # 检查源文件是否存在
    local required_files=(
        "src/recovery/test_integrity.cpp"
        "src/recovery/integrity_checker.cpp"
        "src/recovery/enhanced_block.cpp"
        "src/recovery/crc_checksum.cpp"
        "src/recovery/test_segmented_wal.cpp"
        "src/recovery/segmented_wal.cpp"
        "src/recovery/wal_adapter.cpp"
        "src/recovery/test_integration.cpp"
    )
    
    for file in "${required_files[@]}"; do
        if [ ! -f "$file" ]; then
            log_error "源文件不存在: $file"
            return 1
        fi
    done
    
    # 编译完整性测试
    log_info "编译完整性测试..."
    if g++ -std=c++17 -I src/recovery -o test_integrity \
        src/recovery/test_integrity.cpp \
        src/recovery/integrity_checker.cpp \
        src/recovery/enhanced_block.cpp \
        src/recovery/crc_checksum.cpp \
        -pthread 2>&1; then
        log_success "完整性测试编译成功"
    else
        log_error "完整性测试编译失败"
        log_info "尝试查看编译错误..."
        g++ -std=c++17 -I src/recovery -o test_integrity \
            src/recovery/test_integrity.cpp \
            src/recovery/integrity_checker.cpp \
            src/recovery/enhanced_block.cpp \
            src/recovery/crc_checksum.cpp \
            -pthread
        return 1
    fi
    
    # 编译分段WAL测试
    log_info "编译分段WAL测试..."
    if g++ -std=c++17 -I src/recovery -I src/log -o test_segmented_wal \
        src/recovery/test_segmented_wal.cpp \
        src/recovery/segmented_wal.cpp \
        src/recovery/wal_adapter.cpp \
        src/recovery/crc_checksum.cpp \
        -pthread 2>&1; then
        log_success "分段WAL测试编译成功"
    else
        log_error "分段WAL测试编译失败"
        log_info "尝试查看编译错误..."
        g++ -std=c++17 -I src/recovery -I src/log -o test_segmented_wal \
            src/recovery/test_segmented_wal.cpp \
            src/recovery/segmented_wal.cpp \
            src/recovery/wal_adapter.cpp \
            src/recovery/crc_checksum.cpp \
            -pthread
        return 1
    fi
    
    # 编译集成测试
    log_info "编译集成测试..."
    if g++ -std=c++17 -I src/recovery -o test_integration \
        src/recovery/test_integration.cpp \
        src/recovery/integrity_checker.cpp \
        src/recovery/enhanced_block.cpp \
        src/recovery/crc_checksum.cpp \
        -pthread 2>&1; then
        log_success "集成测试编译成功"
    else
        log_error "集成测试编译失败"
        log_info "尝试查看编译错误..."
        g++ -std=c++17 -I src/recovery -o test_integration \
            src/recovery/test_integration.cpp \
            src/recovery/integrity_checker.cpp \
            src/recovery/enhanced_block.cpp \
            src/recovery/crc_checksum.cpp \
            -pthread
        return 1
    fi
}

# 运行CRC32和完整性检查测试
test_crc32_integrity() {
    log_info "=== CRC32 和完整性检查测试 ==="
    
    run_test "CRC32基础功能测试" "./test_integrity"
}

# 运行WAL分段测试
test_wal_segmentation() {
    log_info "=== WAL 分段机制测试 ==="
    
    run_test "WAL分段功能测试" "./test_segmented_wal"
}

# 运行集成测试
test_integration() {
    log_info "=== 集成测试 ==="
    
    run_test "故障恢复集成测试" "./test_integration"
}

# 运行性能测试
test_performance() {
    log_info "=== 性能测试 ==="
    
    # 创建性能测试脚本
    cat > performance_test.cpp << 'EOF'
#include "segmented_wal.h"
#include "integrity_checker.h"
#include <chrono>
#include <iostream>
#include <filesystem>

int main() {
    std::string test_dir = "/tmp/perf_test_wal";
    
    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    {
        SegmentedWAL wal(test_dir);
        
        // 写入10000个条目
        for (int i = 0; i < 10000; i++) {
            std::string key = "key" + std::to_string(i);
            std::string value = "value" + std::to_string(i) + "_with_some_additional_data_for_realistic_size";
            wal.write_entry(WALEntry::PUT, key, value);
        }
        
        wal.sync_to_disk();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "写入10000个条目耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "平均每个条目: " << (duration.count() / 10000.0) << "ms" << std::endl;
    
    // 测试恢复性能
    start = std::chrono::high_resolution_clock::now();
    
    {
        SegmentedWAL wal(test_dir);
        auto entries = wal.get_all_entries();
        std::cout << "恢复了 " << entries.size() << " 个条目" << std::endl;
    }
    
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "恢复10000个条目耗时: " << duration.count() << "ms" << std::endl;
    
    std::filesystem::remove_all(test_dir);
    
    return 0;
}
EOF
    
    # 编译性能测试
    if g++ -std=c++17 -O2 -I src/recovery -I src/log -o performance_test \
        performance_test.cpp \
        src/recovery/segmented_wal.cpp \
        src/recovery/wal_adapter.cpp \
        src/recovery/crc_checksum.cpp \
        -pthread; then
        
        run_test "性能测试" "./performance_test"
        rm -f performance_test.cpp performance_test
    else
        log_error "性能测试编译失败"
        return 1
    fi
}

# 运行损坏检测测试
test_corruption_detection() {
    log_info "=== 损坏检测测试 ==="
    
    # 创建损坏检测测试脚本
    cat > corruption_test.cpp << 'EOF'
#include "integrity_checker.h"
#include "enhanced_block.h"
#include <iostream>
#include <fstream>
#include <filesystem>

int main() {
    std::cout << "测试损坏检测功能..." << std::endl;
    
    // 创建测试文件
    std::string test_file = "test_corruption.sst";
    {
        std::ofstream out(test_file, std::ios::binary);
        EnhancedSSTableBlock block("test data for corruption detection");
        std::vector<uint8_t> data = block.serialize();
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    
    // 验证原始文件
    IntegrityStatus status = IntegrityChecker::ValidateFile(test_file);
    if (status != IntegrityStatus::OK) {
        std::cerr << "原始文件验证失败" << std::endl;
        return 1;
    }
    std::cout << "✓ 原始文件验证通过" << std::endl;
    
    // 损坏文件
    {
        std::fstream file(test_file, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(50); // 跳到文件中间
        char corrupt_byte = 0xFF;
        file.write(&corrupt_byte, 1);
    }
    
    // 验证损坏的文件
    status = IntegrityChecker::ValidateFile(test_file);
    if (status != IntegrityStatus::CORRUPTION_DETECTED) {
        std::cerr << "损坏检测失败" << std::endl;
        return 1;
    }
    std::cout << "✓ 损坏检测成功" << std::endl;
    
    // 清理
    std::filesystem::remove(test_file);
    
    std::cout << "损坏检测测试通过!" << std::endl;
    return 0;
}
EOF
    
    # 编译损坏检测测试
    if g++ -std=c++17 -I src/recovery -o corruption_test \
        corruption_test.cpp \
        src/recovery/integrity_checker.cpp \
        src/recovery/enhanced_block.cpp \
        src/recovery/crc_checksum.cpp \
        -pthread; then
        
        run_test "损坏检测测试" "./corruption_test"
        rm -f corruption_test.cpp corruption_test
    else
        log_error "损坏检测测试编译失败"
        return 1
    fi
}

# 运行向后兼容性测试
test_backward_compatibility() {
    log_info "=== 向后兼容性测试 ==="
    
    # 创建向后兼容性测试脚本
    cat > compatibility_test.cpp << 'EOF'
#include "enhanced_block.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "测试向后兼容性..." << std::endl;
    
    // 测试从旧格式数据创建增强块
    std::string legacy_data = "key1 123 value1\nkey2 124 value2\n";
    EnhancedSSTableBlock block = EnhancedSSTableBlock::from_legacy_data(legacy_data);
    
    if (!block.verify_integrity()) {
        std::cerr << "从旧格式创建的块完整性验证失败" << std::endl;
        return 1;
    }
    
    if (block.get_data_as_string() != legacy_data) {
        std::cerr << "从旧格式创建的块数据不匹配" << std::endl;
        return 1;
    }
    
    std::cout << "✓ SSTable向后兼容性测试通过" << std::endl;
    
    // 测试WAL向后兼容性
    EnhancedWALBlock wal_block = EnhancedWALBlock::from_legacy_entry("PUT key1 value1", 100);
    
    if (!wal_block.verify_integrity()) {
        std::cerr << "从旧格式创建的WAL块完整性验证失败" << std::endl;
        return 1;
    }
    
    if (wal_block.get_lsn() != 100) {
        std::cerr << "WAL块LSN不匹配" << std::endl;
        return 1;
    }
    
    if (wal_block.get_entry_data_as_string() != "PUT key1 value1") {
        std::cerr << "WAL块数据不匹配" << std::endl;
        return 1;
    }
    
    std::cout << "✓ WAL向后兼容性测试通过" << std::endl;
    
    std::cout << "向后兼容性测试全部通过!" << std::endl;
    return 0;
}
EOF
    
    # 编译向后兼容性测试
    if g++ -std=c++17 -I src/recovery -o compatibility_test \
        compatibility_test.cpp \
        src/recovery/enhanced_block.cpp \
        src/recovery/crc_checksum.cpp \
        -pthread; then
        
        run_test "向后兼容性测试" "./compatibility_test"
        rm -f compatibility_test.cpp compatibility_test
    else
        log_error "向后兼容性测试编译失败"
        return 1
    fi
}

# 清理测试文件
cleanup() {
    log_info "清理测试文件..."
    
    rm -f test_integrity test_segmented_wal test_integration
    rm -f performance_test corruption_test compatibility_test
    rm -f *.sst *.wal
    rm -rf /tmp/test_wal_* /tmp/perf_test_wal
    
    log_success "清理完成"
}

# 显示测试结果摘要
show_summary() {
    echo
    echo "========================================"
    echo "           测试结果摘要"
    echo "========================================"
    echo "总测试数: $TOTAL_TESTS"
    echo -e "通过测试: ${GREEN}$PASSED_TESTS${NC}"
    echo -e "失败测试: ${RED}$FAILED_TESTS${NC}"
    
    if [ $FAILED_TESTS -eq 0 ]; then
        echo -e "${GREEN}🎉 所有测试都通过了！${NC}"
        echo
        echo "故障恢复优化功能验证完成："
        echo "✓ CRC32 校验和保护"
        echo "✓ WAL 分段机制"
        echo "✓ 完整性检查"
        echo "✓ 损坏检测"
        echo "✓ 向后兼容性"
        echo "✓ 性能优化"
        return 0
    else
        echo -e "${RED}❌ 有测试失败，请检查上面的错误信息${NC}"
        return 1
    fi
}

# 主函数
main() {
    echo "========================================"
    echo "    故障恢复优化功能综合测试"
    echo "========================================"
    echo
    
    # 检查构建环境
    check_build_environment
    
    # 编译测试程序
    if ! compile_tests; then
        log_error "测试程序编译失败，退出测试"
        exit 1
    fi
    
    # 运行各项测试
    test_crc32_integrity
    test_wal_segmentation
    test_integration
    test_performance
    test_corruption_detection
    test_backward_compatibility
    
    # 清理
    cleanup
    
    # 显示摘要
    show_summary
}

# 捕获退出信号，确保清理
trap cleanup EXIT

# 运行主函数
main "$@"