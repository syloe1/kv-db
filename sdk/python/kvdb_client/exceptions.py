"""
KVDB Python Client Exceptions
"""


class KVDBError(Exception):
    """KVDB基础异常"""
    pass


class ConnectionError(KVDBError):
    """连接异常"""
    pass


class TimeoutError(KVDBError):
    """超时异常"""
    pass


class AuthenticationError(KVDBError):
    """认证异常"""
    pass


class PermissionError(KVDBError):
    """权限异常"""
    pass


class InvalidArgumentError(KVDBError):
    """无效参数异常"""
    pass


class ResourceNotFoundError(KVDBError):
    """资源未找到异常"""
    pass


class ResourceExistsError(KVDBError):
    """资源已存在异常"""
    pass


class InternalError(KVDBError):
    """内部错误异常"""
    pass