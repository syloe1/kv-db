#!/bin/bash

# 简化的故障恢复测试脚本
# Simplified Fault Recovery Test Script

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 测试现有的可执行文件
test_existing_executables() {
    log_info "测试现有的可执行文件..."
    
    # 测试完整性检查
    if [ -f "src/recovery/test_integrity" ]; then
        log_info "运行完整性测试..."
        if ./src/recovery/test_integrity; then
            log_success "完整性测试通过"
        else
            log_error "完整性测试失败"
        fi
    else
        log_info "完整性测试可执行文件不存在，尝试编译..."
        if g++ -std=c++17 -I src/recovery -o test_integrity_new \
            src/recovery/test_integrity.cpp \
            src/recovery/integrity_checker.cpp \
            src/recovery/enhanced_block.cpp \
            src/recovery/crc_checksum.cpp \
            -pthread; then
            log_success "完整性测试编译成功"
            if ./test_integrity_new; then
                log_success "完整性测试运行成功"
            else
                log_error "完整性测试运行失败"
            fi
            rm -f test_integrity_new
        else
            log_error "完整性测试编译失败"
        fi
    fi
    
    # 测试集成测试
    if [ -f "src/recovery/test_integration" ]; then
        log_info "运行集成测试..."
        if ./src/recovery/test_integration; then
            log_success "集成测试通过"
        else
            log_error "集成测试失败"
        fi
    else
        log_info "集成测试可执行文件不存在，尝试编译..."
        if g++ -std=c++17 -I src/recovery -o test_integration_new \
            src/recovery/test_integration.cpp \
            src/recovery/integrity_checker.cpp \
            src/recovery/enhanced_block.cpp \
            src/recovery/crc_checksum.cpp \
            -pthread; then
            log_success "集成测试编译成功"
            if ./test_integration_new; then
                log_success "集成测试运行成功"
            else
                log_error "集成测试运行失败"
            fi
            rm -f test_integration_new
        else
            log_error "集成测试编译失败"
        fi
    fi
}

# 创建简单的CRC32测试
test_crc32_basic() {
    log_info "创建并运行基础CRC32测试..."
    
    cat > simple_crc_test.cpp << 'EOF'
#include "crc_checksum.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "测试CRC32基础功能..." << std::endl;
    
    // 测试字符串CRC32计算
    std::string test_data = "Hello, KVDB!";
    uint32_t crc1 = CRC32::calculate(test_data);
    uint32_t crc2 = CRC32::calculate(test_data.data(), test_data.size());
    
    std::cout << "CRC32 for '" << test_data << "': " << std::hex << crc1 << std::endl;
    
    if (crc1 != crc2) {
        std::cerr << "CRC32计算不一致!" << std::endl;
        return 1;
    }
    
    // 测试验证功能
    if (!CRC32::verify(test_data.data(), test_data.size(), crc1)) {
        std::cerr << "CRC32验证失败!" << std::endl;
        return 1;
    }
    
    // 测试损坏检测
    std::string corrupted_data = test_data + "x";
    if (CRC32::verify(corrupted_data.data(), corrupted_data.size(), crc1)) {
        std::cerr << "CRC32应该检测到损坏但没有!" << std::endl;
        return 1;
    }
    
    std::cout << "✓ CRC32基础功能测试通过" << std::endl;
    return 0;
}
EOF
    
    if g++ -std=c++17 -I src/recovery -o simple_crc_test \
        simple_crc_test.cpp \
        src/recovery/crc_checksum.cpp; then
        log_success "CRC32测试编译成功"
        if ./simple_crc_test; then
            log_success "CRC32测试运行成功"
        else
            log_error "CRC32测试运行失败"
        fi
        rm -f simple_crc_test simple_crc_test.cpp
    else
        log_error "CRC32测试编译失败"
        rm -f simple_crc_test.cpp
    fi
}

# 创建简单的块测试
test_enhanced_blocks() {
    log_info "创建并运行增强块测试..."
    
    cat > simple_block_test.cpp << 'EOF'
#include "enhanced_block.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "测试增强块功能..." << std::endl;
    
    try {
        // 测试SSTable块
        std::string test_data = "key1 123 value1\nkey2 124 value2";
        EnhancedSSTableBlock sstable_block(test_data);
        
        if (!sstable_block.verify_integrity()) {
            std::cerr << "SSTable块完整性验证失败!" << std::endl;
            return 1;
        }
        
        // 测试序列化和反序列化
        std::vector<uint8_t> serialized = sstable_block.serialize();
        EnhancedSSTableBlock deserialized = EnhancedSSTableBlock::deserialize(serialized);
        
        if (!deserialized.verify_integrity()) {
            std::cerr << "反序列化后的SSTable块完整性验证失败!" << std::endl;
            return 1;
        }
        
        if (deserialized.get_data_as_string() != test_data) {
            std::cerr << "反序列化后的数据不匹配!" << std::endl;
            return 1;
        }
        
        std::cout << "✓ SSTable块测试通过" << std::endl;
        
        // 测试WAL块
        EnhancedWALBlock wal_block(100, "PUT key1 value1");
        
        if (!wal_block.verify_integrity()) {
            std::cerr << "WAL块完整性验证失败!" << std::endl;
            return 1;
        }
        
        if (wal_block.get_lsn() != 100) {
            std::cerr << "WAL块LSN不匹配!" << std::endl;
            return 1;
        }
        
        std::cout << "✓ WAL块测试通过" << std::endl;
        
        std::cout << "✓ 增强块功能测试全部通过" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "测试异常: " << e.what() << std::endl;
        return 1;
    }
}
EOF
    
    if g++ -std=c++17 -I src/recovery -o simple_block_test \
        simple_block_test.cpp \
        src/recovery/enhanced_block.cpp \
        src/recovery/crc_checksum.cpp; then
        log_success "增强块测试编译成功"
        if ./simple_block_test; then
            log_success "增强块测试运行成功"
        else
            log_error "增强块测试运行失败"
        fi
        rm -f simple_block_test simple_block_test.cpp
    else
        log_error "增强块测试编译失败"
        rm -f simple_block_test.cpp
    fi
}

# 测试分段WAL基础功能
test_segmented_wal_basic() {
    log_info "测试分段WAL基础功能..."
    
    # 检查是否有现成的测试可执行文件
    if [ -f "test_segmented_wal" ]; then
        log_info "运行现有的分段WAL测试..."
        if ./test_segmented_wal; then
            log_success "分段WAL测试通过"
        else
            log_error "分段WAL测试失败"
        fi
    else
        log_info "分段WAL测试可执行文件不存在，跳过此测试"
    fi
}

# 显示功能状态
show_feature_status() {
    echo
    echo "========================================"
    echo "        故障恢复优化功能状态"
    echo "========================================"
    
    # 检查各个组件的实现状态
    echo "📁 源文件状态:"
    
    if [ -f "src/recovery/crc_checksum.cpp" ]; then
        echo "  ✓ CRC32校验和实现"
    else
        echo "  ✗ CRC32校验和实现"
    fi
    
    if [ -f "src/recovery/enhanced_block.cpp" ]; then
        echo "  ✓ 增强块结构实现"
    else
        echo "  ✗ 增强块结构实现"
    fi
    
    if [ -f "src/recovery/integrity_checker.cpp" ]; then
        echo "  ✓ 完整性检查器实现"
    else
        echo "  ✗ 完整性检查器实现"
    fi
    
    if [ -f "src/recovery/segmented_wal.cpp" ]; then
        echo "  ✓ 分段WAL实现"
    else
        echo "  ✗ 分段WAL实现"
    fi
    
    echo
    echo "🧪 测试文件状态:"
    
    if [ -f "src/recovery/test_integrity.cpp" ]; then
        echo "  ✓ 完整性测试"
    else
        echo "  ✗ 完整性测试"
    fi
    
    if [ -f "src/recovery/test_integration.cpp" ]; then
        echo "  ✓ 集成测试"
    else
        echo "  ✗ 集成测试"
    fi
    
    if [ -f "src/recovery/test_segmented_wal.cpp" ]; then
        echo "  ✓ 分段WAL测试"
    else
        echo "  ✗ 分段WAL测试"
    fi
    
    echo
    echo "📋 规范文档状态:"
    
    if [ -f ".kiro/specs/fault-recovery-optimization/requirements.md" ]; then
        echo "  ✓ 需求文档"
    else
        echo "  ✗ 需求文档"
    fi
    
    if [ -f ".kiro/specs/fault-recovery-optimization/design.md" ]; then
        echo "  ✓ 设计文档"
    else
        echo "  ✗ 设计文档"
    fi
    
    if [ -f ".kiro/specs/fault-recovery-optimization/tasks.md" ]; then
        echo "  ✓ 任务列表"
    else
        echo "  ✗ 任务列表"
    fi
}

# 主函数
main() {
    echo "========================================"
    echo "    故障恢复优化功能简化测试"
    echo "========================================"
    echo
    
    # 显示功能状态
    show_feature_status
    
    echo
    echo "🧪 开始运行测试..."
    echo
    
    # 运行各项测试
    test_existing_executables
    test_crc32_basic
    test_enhanced_blocks
    test_segmented_wal_basic
    
    echo
    echo "========================================"
    echo "           测试完成"
    echo "========================================"
    echo
    echo "💡 提示:"
    echo "  - 如需运行完整测试，请使用: bash test_fault_recovery_optimization.sh"
    echo "  - 如需查看任务列表，请打开: .kiro/specs/fault-recovery-optimization/tasks.md"
    echo "  - 如需执行具体任务，请在任务文件中点击相应任务"
}

# 运行主函数
main "$@"