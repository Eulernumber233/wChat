"""Process-local dict-backed MemoryStore.

Fine for single-node dev/testing. In production we'll swap for Redis
so multiple AgentServer replicas share sessions. Not thread-safe across
workers — acceptable while we run a single uvicorn worker.
"""
from __future__ import annotations

import asyncio

from .base import MemoryStore, SessionRecord


class InMemoryStore:
    def __init__(self) -> None:
        self._data: dict[str, SessionRecord] = {}
        self._lock = asyncio.Lock()

    async def put(self, record: SessionRecord) -> None:
        async with self._lock:
            self._data[record.session_id] = record

    async def get(self, session_id: str) -> SessionRecord | None:
        async with self._lock:
            return self._data.get(session_id)

    async def delete(self, session_id: str) -> None:
        async with self._lock:
            self._data.pop(session_id, None)


_cached: MemoryStore | None = None


# 获取MemoryStore实例的函数，使用不同的存储方式
def get_memory_store() -> MemoryStore:
    """Return the configured MemoryStore. Backend comes from settings."""
    global _cached
    if _cached is not None:
        return _cached

    # imported lazily to keep the import graph shallow for tests that only
    # need InMemoryStore
    from ..config.settings import get_settings

    s = get_settings()
    if s.memory.backend == "redis":
        from ..redis_client import get_redis
        from .redis_store import RedisSessionStore

        _cached = RedisSessionStore(
            redis=get_redis(),
            ttl_seconds=s.memory.session_ttl_seconds,
            key_template=s.memory.session_key_template,
        )
    else:
        _cached = InMemoryStore()
    return _cached


def reset_memory_store() -> None:
    """Test hook: clear the cached store so the next call re-reads settings."""
    global _cached
    _cached = None
