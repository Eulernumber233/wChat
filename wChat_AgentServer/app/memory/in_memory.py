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


def get_memory_store() -> MemoryStore:
    global _cached
    if _cached is None:
        _cached = InMemoryStore()
    return _cached
