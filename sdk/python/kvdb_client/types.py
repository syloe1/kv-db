"""
KVDB Python Client Types
"""

from dataclasses import dataclass
from typing import Optional


@dataclass
class KeyValue:
    """键值对"""
    key: str
    value: str


@dataclass
class ScanOptions:
    """扫描选项"""
    start_key: str = ""
    end_key: str = ""
    prefix: str = ""
    limit: int = 1000
    reverse: bool = False


@dataclass
class Snapshot:
    """快照"""
    snapshot_id: int
    
    def __str__(self):
        return f"Snapshot(id={self.snapshot_id})"