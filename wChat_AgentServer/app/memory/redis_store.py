"""Redis-backed MemoryStore.

Survives process restart; shared across multiple AgentServer replicas.
Keys: `agent_session:<sid>` (template configurable via settings).
Value: JSON-encoded SessionRecord. TTL refreshed on each put().

Schema evolution: new fields should be added optionally so older snapshots
still deserialize.
"""
from __future__ import annotations

import json
from dataclasses import asdict
from typing import Any

from redis.asyncio import Redis

from .base import SessionRecord


class RedisSessionStore:
    def __init__(self, redis: Redis, ttl_seconds: int, key_template: str) -> None:
        self._r = redis
        self._ttl = ttl_seconds
        self._tmpl = key_template

    def _key(self, session_id: str) -> str:
        return self._tmpl.format(sid=session_id)

    async def put(self, record: SessionRecord) -> None:
        payload = json.dumps(asdict(record), ensure_ascii=False)
        await self._r.set(self._key(record.session_id), payload, ex=self._ttl)

    async def get(self, session_id: str) -> SessionRecord | None:
        raw = await self._r.get(self._key(session_id))
        if raw is None:
            return None
        data: dict[str, Any] = json.loads(raw)
        # tolerate missing fields for forward compat
        return SessionRecord(
            session_id=data.get("session_id", session_id),
            self_uid=int(data.get("self_uid", 0)),
            peer_uid=int(data.get("peer_uid", 0)),
            state_snapshot=data.get("state_snapshot") or {},
            created_at=float(data.get("created_at") or 0.0),
        )

    async def delete(self, session_id: str) -> None:
        await self._r.delete(self._key(session_id))
