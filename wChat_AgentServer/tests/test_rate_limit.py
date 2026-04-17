"""check_and_incr: budget enforcement + fail-open on Redis errors."""
from __future__ import annotations

import pytest
from fastapi import HTTPException

from app.config.settings import get_settings
from app.security.rate_limit import check_and_incr


async def test_under_budget_ok(fake_redis):
    s = get_settings()
    s.rate_limit.per_user_per_day = 3
    s.rate_limit.enabled = True
    for _ in range(3):
        await check_and_incr(1001)  # no raise


async def test_over_budget_raises_429(fake_redis):
    s = get_settings()
    s.rate_limit.per_user_per_day = 2
    s.rate_limit.enabled = True
    await check_and_incr(1001)
    await check_and_incr(1001)
    with pytest.raises(HTTPException) as exc:
        await check_and_incr(1001)
    assert exc.value.status_code == 429


async def test_disabled_short_circuits(fake_redis):
    s = get_settings()
    s.rate_limit.per_user_per_day = 1
    s.rate_limit.enabled = False
    # should never raise, no matter how many calls
    for _ in range(10):
        assert await check_and_incr(1001) == 0


async def test_fail_open_on_redis_error(fake_redis, monkeypatch):
    """If Redis INCR throws, we don't 500 every caller."""
    s = get_settings()
    s.rate_limit.enabled = True
    s.rate_limit.per_user_per_day = 100

    async def boom(*args, **kwargs):
        raise RuntimeError("redis down")

    monkeypatch.setattr(fake_redis, "incr", boom)
    # Must not raise — matches ChatServer ValidateSession fail-open policy
    assert await check_and_incr(1001) == 0


async def test_expire_set_on_first_hit(fake_redis):
    s = get_settings()
    s.rate_limit.enabled = True
    s.rate_limit.per_user_per_day = 100
    await check_and_incr(7777)
    # fakeredis reflects TTL; exact value depends on time-of-day, just check >0
    from datetime import UTC, datetime

    key = f"agent_quota_7777_{datetime.now(UTC).strftime('%Y%m%d')}"
    ttl = await fake_redis.ttl(key)
    assert ttl > 0
