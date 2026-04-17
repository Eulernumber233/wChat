"""require_auth dependency: header parsing + Redis validation."""
from __future__ import annotations

import pytest
from fastapi import HTTPException

from app.config.settings import get_settings
from app.security.auth import enforce_self_uid, AuthContext, require_auth


async def _call(authorization=None, x_self_uid=None):
    return await require_auth(authorization=authorization, x_self_uid=x_self_uid)


async def test_missing_authorization_401(fake_redis):
    with pytest.raises(HTTPException) as exc:
        await _call()
    assert exc.value.status_code == 401


async def test_malformed_authorization_401(fake_redis):
    with pytest.raises(HTTPException) as exc:
        await _call(authorization="Token abc", x_self_uid="1")
    assert exc.value.status_code == 401


async def test_missing_x_self_uid_400(fake_redis):
    with pytest.raises(HTTPException) as exc:
        await _call(authorization="Bearer t", x_self_uid=None)
    assert exc.value.status_code == 400


async def test_non_int_x_self_uid_400(fake_redis):
    with pytest.raises(HTTPException) as exc:
        await _call(authorization="Bearer t", x_self_uid="abc")
    assert exc.value.status_code == 400


async def test_token_not_in_redis_401(fake_redis):
    get_settings().auth.mode = "redis"
    with pytest.raises(HTTPException) as exc:
        await _call(authorization="Bearer t", x_self_uid="1001")
    assert exc.value.status_code == 401


async def test_token_mismatch_401(fake_redis):
    s = get_settings()
    s.auth.mode = "redis"
    await fake_redis.set(s.auth.token_key_template.format(uid=1001), "real-token")
    with pytest.raises(HTTPException) as exc:
        await _call(authorization="Bearer wrong", x_self_uid="1001")
    assert exc.value.status_code == 401


async def test_token_ok_returns_context(fake_redis):
    s = get_settings()
    s.auth.mode = "redis"
    await fake_redis.set(s.auth.token_key_template.format(uid=1001), "real-token")
    ctx = await _call(authorization="Bearer real-token", x_self_uid="1001")
    assert isinstance(ctx, AuthContext)
    assert ctx.self_uid == 1001
    assert ctx.token == "real-token"


async def test_auth_mode_off_skips_redis(fake_redis):
    get_settings().auth.mode = "off"
    ctx = await _call(authorization="Bearer whatever", x_self_uid="42")
    assert ctx.self_uid == 42


def test_enforce_self_uid_mismatch():
    ctx = AuthContext(self_uid=1, token="t")
    with pytest.raises(HTTPException) as exc:
        enforce_self_uid(ctx, body_self_uid=2)
    assert exc.value.status_code == 403


def test_enforce_self_uid_ok():
    ctx = AuthContext(self_uid=1, token="t")
    enforce_self_uid(ctx, body_self_uid=1)  # no raise
