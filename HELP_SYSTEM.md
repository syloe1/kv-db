# KVDB 帮助系统使用指南

## 概述

KVDB 现在支持完整的帮助系统，包括 MAN 页面和多种 HELP 命令格式。

## 功能特性

### 1. 基本帮助命令
```bash
kvdb> HELP
```
显示所有可用命令的简要列表。

### 2. MAN 系统
```bash
kvdb> MAN <command>
```
显示指定命令的详细手册页，包括：
- 命令名称和描述
- 语法格式
- 详细说明
- 参数说明
- 使用示例
- 返回值说明
- 相关命令

#### 示例：
```bash
kvdb> MAN PUT
kvdb> MAN SCAN
kvdb> MAN SNAPSHOT
```

### 3. 命令级帮助
支持两种格式：

#### 格式1：`COMMAND HELP`
```bash
kvdb> PUT HELP
kvdb> GET HELP
kvdb> SCAN HELP
```

#### 格式2：`COMMAND - HELP`
```bash
kvdb> PUT - HELP
kvdb> GET - HELP
kvdb> SCAN - HELP
```

## 支持的命令

所有命令都支持完整的帮助文档：

- **PUT** - 插入或更新键值对
- **GET** - 获取键的值
- **DEL** - 删除键
- **FLUSH** - 刷新 MemTable 到磁盘
- **COMPACT** - 触发手动压缩
- **SNAPSHOT** - 创建数据库快照
- **GET_AT** - 在指定快照获取值
- **RELEASE** - 释放快照
- **SCAN** - 范围扫描
- **STATS** - 显示数据库统计信息
- **LSM** - 显示 LSM 树结构
- **HELP** - 显示帮助信息
- **MAN** - 显示命令手册

## 使用示例

### 查看 PUT 命令的详细帮助：
```bash
kvdb> MAN PUT
NAME
    PUT - Insert or update a key-value pair

SYNOPSIS
    PUT <key> <value>

DESCRIPTION
    The PUT command inserts a new key-value pair into the database or
    updates an existing key with a new value. The operation is atomic
    and will be written to the Write-Ahead Log (WAL) before being
    stored in the MemTable.

PARAMETERS
    key     The key to insert or update (string)
    value   The value to associate with the key (string)

EXAMPLES
    PUT user:1 "John Doe"
    PUT config:timeout 30
    PUT "my key" "my value"

RETURN VALUE
    Returns "OK" on success

SEE ALSO
    GET, DEL, FLUSH
```

### 快速查看命令帮助：
```bash
kvdb> SCAN HELP
# 显示与 MAN SCAN 相同的详细信息
```

## 错误处理

如果请求不存在的命令帮助：
```bash
kvdb> MAN INVALID
No manual entry for 'INVALID'
Available commands: PUT, GET, DEL, FLUSH, COMPACT, SNAPSHOT, GET_AT, RELEASE, SCAN, STATS, LSM, HELP, MAN
Use 'HELP' to see all commands or 'MAN <command>' for specific help.
```

## 实现特点

1. **统一的帮助格式**：所有命令都遵循标准的 UNIX man page 格式
2. **多种访问方式**：支持 MAN 命令和内联 HELP 格式
3. **完整的文档**：每个命令都包含语法、描述、示例和相关命令
4. **错误友好**：对无效命令提供有用的错误信息和建议
5. **自动补全支持**：MAN 命令也支持 TAB 自动补全

这个帮助系统让用户能够快速了解和学习 KVDB 的所有功能，无需查阅外部文档。