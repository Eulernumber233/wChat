"""Process-wide async Redis client.

One connection pool per process. Tests inject a fakeredis instance via
`set_redis_client(...)` so no real Redis is required.

Intentionally kept at `app/redis_client.py` (not under a package like
`app/redis/`) to avoid shadowing the third-party `redis` module.
"""
from __future__ import annotations

from typing import Any

from redis.asyncio import Redis

from .config.settings import get_settings

_client: Redis | None = None


def _build_from_settings() -> Redis:
    s = get_settings().redis
    return Redis(
        host=s.host,
        port=s.port,
        password=s.password or None,
        db=s.db,
        decode_responses=True,  # str in, str out; keeps call sites simple
    )

# 单例redis客户端
def get_redis() -> Redis:
    """Lazy singleton. FastAPI deps + services call this."""
    global _client
    if _client is None:
        _client = _build_from_settings()
    return _client


def set_redis_client(client: Any) -> None:
    """Test hook: inject a fakeredis.aioredis.FakeRedis."""
    global _client
    _client = client


def reset_redis_client() -> None:
    global _client
    _client = None
