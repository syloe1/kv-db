#!/bin/bash

echo "=== KVDB 性能测试：堆优化 MergeIterator 和 Prefix Scan ==="
echo

# 清理之前的数据
rm -f kvdb.log data/sstable_*.dat data/MANIFEST

echo "1. 插入测试数据..."
{
    # 插入大量数据
    for i in {1..50}; do
        echo "PUT user:$(printf "%03d" $i) User_$i"
        echo "PUT config:setting_$(printf "%03d" $i) Value_$i"
        echo "PUT app:module_$(printf "%03d" $i) Module_$i"
    done
    
    # 强制刷盘创建多个 SSTable
    echo "FLUSH"
    
    # 插入更多数据
    for i in {51..100}; do
        echo "PUT user:$(printf "%03d" $i) User_$i"
        echo "PUT config:setting_$(printf "%03d" $i) Value_$i"
    done
    
    echo "FLUSH"
    
    # 测试 PREFIX_SCAN 性能
    echo "PREFIX_SCAN user:"
    echo "PREFIX_SCAN config:"
    echo "PREFIX_SCAN app:"
    
    # 比较 SCAN 和 PREFIX_SCAN
    echo "SCAN user:001 user:999"
    
    echo "STATS"
    echo "LSM"
    echo "EXIT"
} | ./build/kvdb

echo
echo "=== 测试完成 ==="