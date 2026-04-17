"""Bearer token auth.

Client sends `Authorization: Bearer <token>` (the same `utoken_<uid>`
StatusServer wrote at login). AgentServer has its own Redis connection
and validates the token directly — no ChatServer round-trip per request.

Modes (settings.auth.mode):
  - "redis": token must equal the string stored at `utoken_<uid>`
  - "off":   accept anything, used only for local dev / unit tests that
             don't want to spin fakeredis

Returns the authenticated `AuthContext` via FastAPI dependency; routes
depend on it to also enforce `body.self_uid == ctx.self_uid`.
"""
from __future__ import annotations

import logging
from dataclasses import dataclass

from fastapi import Depends, Header, HTTPException, status

from ..config.settings import get_settings
from ..redis_client import get_redis


@dataclass
class AuthContext:
    self_uid: int
    token: str


log = logging.getLogger(__name__)


def _parse_bearer(authorization: str | None) -> str:
    if not authorization:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="missing Authorization header",
        )
    parts = authorization.split(None, 1)
    if len(parts) != 2 or parts[0].lower() != "bearer" or not parts[1].strip():
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail=f"malformed Authorization header (got: '{authorization[:40]}')",
        )
    return parts[1].strip()


async def require_auth(
    authorization: str | None = Header(default=None),
    x_self_uid: str | None = Header(default=None, alias="X-Self-Uid"),
) -> AuthContext:
    settings = get_settings()
    token = _parse_bearer(authorization)

    if not x_self_uid:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="missing X-Self-Uid header",
        )
    try:
        self_uid = int(x_self_uid)
    except ValueError as e:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="X-Self-Uid must be an integer",
        ) from e

    if settings.auth.mode == "off":
        return AuthContext(self_uid=self_uid, token=token)

    key = settings.auth.token_key_template.format(uid=self_uid)
    redis = get_redis()
    try:
        stored = await redis.get(key)
    except Exception as e:
        log.error("Redis GET %s failed: %s", key, e)
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail=f"redis error: {e}",
        ) from e

    log.warning("AUTH uid=%s key=%s stored=%s bearer=%s match=%s",
                self_uid, key,
                stored[:8] + "..." if stored else "None",
                token[:8] + "..." if token else "None",
                stored == token if stored else "N/A")

    if stored is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail=f"token not found in Redis (key={key})",
        )
    # decode_responses=True in the client → both are str
    if stored != token:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="token mismatch",
        )
    return AuthContext(self_uid=self_uid, token=token)


def enforce_self_uid(ctx: AuthContext, body_self_uid: int) -> None:
    """Routes call this after body is parsed."""
    if ctx.self_uid != body_self_uid:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="self_uid in body does not match authenticated uid",
        )


# convenience alias for type-hinted deps
AuthDep = Depends(require_auth)
