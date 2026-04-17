"""Per-user daily rate limit backed by Redis.

Key: `agent_quota_<uid>_<YYYYMMDD>`  (UTC date)
Op:  INCR then (on first hit) EXPIRE to the end-of-day in UTC.

Why UTC: AgentServer replicas may run on machines with different
timezones; UTC gives a single unambiguous rollover point.

`check_and_incr` returns the new count. If the count exceeds the daily
budget, the caller raises HTTP 429. Disabled modes return immediately
(count=0) so production flags can turn the limiter off cheaply.
"""
from __future__ import annotations

from datetime import UTC, datetime
from typing import Protocol

from fastapi import HTTPException, status

from ..config.settings import get_settings
from ..redis_client import get_redis

# 这便于单元测试时用 fakeredis 或 mock 对象替换真实 Redis
class _RedisLike(Protocol):
    async def incr(self, name: str) -> int: ...  # pragma: no cover - proto
    async def expireat(self, name: str, when: int) -> bool: ...  # pragma: no cover


def _today_utc_str() -> str:
    return datetime.now(UTC).strftime("%Y%m%d")


def _end_of_day_utc_epoch() -> int:
    now = datetime.now(UTC)
    # 23:59:59 today UTC; Redis EXPIREAT takes seconds since epoch
    eod = now.replace(hour=23, minute=59, second=59, microsecond=0)
    return int(eod.timestamp())


async def check_and_incr(self_uid: int) -> int:
    """Increment quota counter; raise 429 if over budget.

    Returns the new count on success. On Redis errors we log via the
    HTTPException (no — we fail open): the IM path has a similar
    policy (Redis wobble shouldn't 500 every user).
    """
    settings = get_settings()
    rl = settings.rate_limit
    if not rl.enabled:
        return 0

    redis = get_redis()
    key = f"agent_quota_{self_uid}_{_today_utc_str()}"
    try:
        count = await redis.incr(key)
        if count == 1:
            # first hit today — set expiry so stale counters don't pile up
            await redis.expireat(key, _end_of_day_utc_epoch())
    except Exception:
        # fail open on Redis issues, same philosophy as ValidateSession on
        # the ChatServer side (§8.6 第 2 层)
        return 0

    if count > rl.per_user_per_day:
        raise HTTPException(
            status_code=status.HTTP_429_TOO_MANY_REQUESTS,
            detail=f"daily quota exceeded ({rl.per_user_per_day})",
        )
    return int(count)
