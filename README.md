# KV Database

## Write Flow
```mermaid
flowchart TD
    A[put(key, value)] --> B[WAL: log_put(key, value)]
    B --> C[MemTable: put(key, value)]
```

## Why WAL Must Be Written Before MemTable?
1. **Durability**: WAL ensures data is persisted to disk before updating the in-memory structure (MemTable). If the system crashes after writing to WAL but before updating MemTable, the data can be recovered from WAL.
2. **Atomicity**: The operation is only considered complete once both WAL and MemTable are updated. If the system crashes during the operation, the recovery process can replay the WAL to ensure consistency.
3. **Safety**: Writing to WAL first guarantees that even if the system crashes immediately after, the operation can be replayed to restore the database state.