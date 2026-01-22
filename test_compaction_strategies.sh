#!/bin/bash

echo "=== KVDB 压缩策略测试 ==="
echo

# 清理之前的数据
rm -f kvdb.log data/sstable_*.dat data/MANIFEST

echo "1. 测试默认 Leveled 压缩策略..."
{
    # 插入大量数据触发压缩
    for i in {1..100}; do
        echo "PUT test:$(printf "%03d" $i) value_$i"
    done
    
    echo "FLUSH"
    echo "STATS"
    echo "LSM"
    
    # 切换到 Tiered 策略
    echo "SET_COMPACTION TIERED"
    
    # 插入更多数据
    for i in {101..200}; do
        echo "PUT test:$(printf "%03d" $i) value_$i"
    done
    
    echo "FLUSH"
    echo "COMPACT"
    echo "STATS"
    
    # 测试 Size-Tiered 策略
    echo "SET_COMPACTION SIZE_TIERED"
    
    for i in {201..300}; do
        echo "PUT test:$(printf "%03d" $i) value_$i"
    done
    
    echo "FLUSH"
    echo "COMPACT"
    echo "STATS"
    
    # 测试 Time Window 策略
    echo "SET_COMPACTION TIME_WINDOW"
    
    for i in {301..400}; do
        echo "PUT test:$(printf "%03d" $i) value_$i"
    done
    
    echo "FLUSH"
    echo "COMPACT"
    echo "STATS"
    echo "LSM"
    
    echo "EXIT"
} | ./build/kvdb

echo
echo "=== 压缩策略测试完成 ==="