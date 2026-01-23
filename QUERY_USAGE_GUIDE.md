# KVDB 高级查询功能使用指南

## 概述

KVDB 现已支持完整的高级查询功能，包括批量操作、条件查询、聚合查询和排序查询。这些功能通过增强的CLI界面提供，让您能够高效地处理复杂的数据查询需求。

## 功能特性

### 1. 批量操作 (Batch Operations)

批量操作允许您在单个命令中处理多个键值对，显著提高操作效率。

#### 批量插入 (Batch PUT)
```bash
BATCH PUT key1 value1 key2 value2 key3 value3 ...
```

**示例：**
```bash
kvdb> BATCH PUT user:1 Alice user:2 Bob user:3 Charlie
Batch PUT completed: 3 key-value pairs inserted

kvdb> BATCH PUT score:1 95 score:2 87 score:3 92
Batch PUT completed: 3 key-value pairs inserted
```

#### 批量获取 (Batch GET)
```bash
BATCH GET key1 key2 key3 ...
```

**示例：**
```bash
kvdb> BATCH GET user:1 user:2 user:3 user:4
=== Batch GET Results ===
user:1 = Alice
user:2 = Bob
user:3 = Charlie
Found 3 out of 4 keys
```

#### 批量删除 (Batch DEL)
```bash
BATCH DEL key1 key2 key3 ...
```

**示例：**
```bash
kvdb> BATCH DEL temp:1 temp:2 temp:3
Batch DELETE completed: 3 keys deleted
```

### 2. 条件查询 (Conditional Queries)

条件查询支持基于键或值的模式匹配和比较操作。

#### 基本语法
```bash
GET_WHERE <field> <operator> <value> [LIMIT <n>]
```

**字段 (field):**
- `key` - 对键进行条件匹配
- `value` - 对值进行条件匹配

**操作符 (operator):**
- `=` 或 `==` - 等于
- `!=` 或 `<>` - 不等于
- `LIKE` - 模式匹配（支持通配符 `*` 和 `?`）
- `NOT_LIKE` - 反向模式匹配
- `>` - 大于
- `<` - 小于
- `>=` - 大于等于
- `<=` - 小于等于

#### 示例

**模式匹配查询：**
```bash
kvdb> GET_WHERE key LIKE 'user:*'
=== Query Results ===
user:1 = Alice
user:2 = Bob
user:3 = Charlie
Found 3 matching records

kvdb> GET_WHERE key LIKE 'score:?'
=== Query Results ===
score:1 = 95
score:2 = 87
score:3 = 92
Found 3 matching records
```

**数值比较查询：**
```bash
kvdb> GET_WHERE value > '90'
=== Query Results ===
score:1 = 95
score:3 = 92
Found 2 matching records

kvdb> GET_WHERE value <= '50' LIMIT 5
=== Query Results ===
price:2 = 25
Found 1 matching records
```

**精确匹配查询：**
```bash
kvdb> GET_WHERE key = 'config:timeout'
=== Query Results ===
config:timeout = 30
Found 1 matching records
```

### 3. 聚合查询 (Aggregate Queries)

聚合查询提供统计分析功能，支持计数、求和、平均值和最值计算。

#### 计数查询 (COUNT)
```bash
COUNT                           # 统计所有记录
COUNT WHERE <field> <op> <val>  # 条件计数
```

**示例：**
```bash
kvdb> COUNT
Total records: 15

kvdb> COUNT WHERE key LIKE 'user:*'
Matching records: 3

kvdb> COUNT WHERE value > '80'
Matching records: 5
```

#### 数值统计 (SUM/AVG/MIN_MAX)
```bash
SUM [pattern]      # 求和
AVG [pattern]      # 平均值
MIN_MAX [pattern]  # 最小值和最大值
```

**示例：**
```bash
kvdb> SUM 'score:*'
=== Sum Results ===
Count: 3
Sum: 274
Average: 91.33

kvdb> AVG 'price:*'
=== Average Results ===
Count: 3
Sum: 1305
Average: 435

kvdb> MIN_MAX 'temperature:*'
=== Min/Max Results ===
Count: 3
Sum: 71.3
Average: 23.77
Minimum: 22.8
Maximum: 25.0
```

### 4. 排序查询 (Ordered Queries)

排序查询支持按键的升序或降序扫描，并可限制结果数量。

#### 基本语法
```bash
SCAN_ORDER <ASC|DESC> [start_key] [end_key] [LIMIT <n>]
```

**示例：**

**全表排序扫描：**
```bash
kvdb> SCAN_ORDER ASC
=== Ordered Scan Results ===
config:timeout = 30
item:a = first
item:b = second
item:c = third
price:1 = 1200
score:1 = 95
user:1 = Alice
Found 7 records (order: ASC)

kvdb> SCAN_ORDER DESC LIMIT 5
=== Ordered Scan Results ===
user:3 = Charlie
user:2 = Bob
user:1 = Alice
score:3 = 92
score:2 = 87
Found 5 records (order: DESC)
```

**范围排序扫描：**
```bash
kvdb> SCAN_ORDER ASC user:1 user:9
=== Ordered Scan Results ===
user:1 = Alice
user:2 = Bob
user:3 = Charlie
Found 3 records (order: ASC)

kvdb> SCAN_ORDER DESC score: score:z LIMIT 2
=== Ordered Scan Results ===
score:3 = 92
score:2 = 87
Found 2 records (order: DESC)
```

## 实际应用场景

### 电商场景
```bash
# 添加商品数据
BATCH PUT product:1 laptop product:2 mouse product:3 keyboard
BATCH PUT price:1 1200 price:2 25 price:3 80
BATCH PUT category:1 electronics category:2 electronics category:3 electronics

# 查找所有商品
GET_WHERE key LIKE 'product:*'

# 查找高价商品
GET_WHERE value > '100'

# 统计商品数量
COUNT WHERE key LIKE 'product:*'

# 计算平均价格
AVG 'price:*'

# 按价格排序查看商品
SCAN_ORDER ASC price:1 price:999
```

### 用户管理场景
```bash
# 批量添加用户
BATCH PUT user:1001 Alice user:1002 Bob user:1003 Charlie
BATCH PUT email:1001 alice@example.com email:1002 bob@example.com

# 查找所有用户
GET_WHERE key LIKE 'user:*'

# 统计用户数量
COUNT WHERE key LIKE 'user:*'

# 按用户ID排序
SCAN_ORDER ASC user:1000 user:1999
```

### 监控数据场景
```bash
# 添加监控数据
BATCH PUT cpu:server1 85.5 cpu:server2 72.3 cpu:server3 91.2
BATCH PUT memory:server1 78.9 memory:server2 65.4 memory:server3 88.7

# 查找高CPU使用率服务器
GET_WHERE value > '80'

# 计算平均CPU使用率
AVG 'cpu:*'

# 查找CPU和内存使用率范围
MIN_MAX 'cpu:*'
MIN_MAX 'memory:*'

# 统计监控指标数量
COUNT
```

## 性能特性

### 优化特性
- **批量操作**: 减少网络往返和WAL同步开销
- **前缀查询**: 利用LSM树的有序特性进行高效前缀扫描
- **流式处理**: 支持LIMIT的大结果集流式处理
- **内存优化**: 避免大结果集的内存占用

### 性能建议
1. **使用批量操作**: 对于多个键的操作，优先使用BATCH命令
2. **合理使用LIMIT**: 对于大结果集查询，使用LIMIT限制返回数量
3. **优化查询条件**: 使用更具体的模式匹配减少扫描范围
4. **利用键的设计**: 设计有意义的键前缀以便高效的前缀查询

## 错误处理

查询操作包含完善的错误处理机制：

```bash
# 无效操作符
kvdb> GET_WHERE key INVALID 'value'
Invalid operator: INVALID

# 无效LIMIT值
kvdb> SCAN_ORDER ASC LIMIT abc
Invalid limit value: abc

# 查询失败时的错误信息
kvdb> GET_WHERE key LIKE 'pattern'
Query failed: [具体错误信息]
```

## 总结

KVDB的高级查询功能为您提供了强大而灵活的数据查询能力：

- ✅ **批量操作**: 提高多键操作效率
- ✅ **条件查询**: 支持复杂的模式匹配和比较
- ✅ **聚合查询**: 提供统计分析功能
- ✅ **排序查询**: 支持有序数据扫描
- ✅ **性能优化**: 充分利用LSM树特性
- ✅ **易于使用**: 直观的SQL风格命令语法

这些功能使KVDB从简单的键值存储升级为功能丰富的查询数据库，能够满足更复杂的应用场景需求。