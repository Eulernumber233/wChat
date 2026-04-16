"""Short-term agent session memory.

Holds enough state to support `/refine` follow-ups: the final AgentState
after `suggest_reply`, so we can regenerate a single candidate without
re-running the whole graph. Future: back this with Redis (TTL ~1h).
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Protocol


@dataclass
class SessionRecord:
    session_id: str
    self_uid: int
    peer_uid: int
    # final AgentState dump from suggest_reply; opaque to the store
    state_snapshot: dict[str, Any] = field(default_factory=dict)
    created_at: float = 0.0


class MemoryStore(Protocol):
    async def put(self, record: SessionRecord) -> None: ...
    async def get(self, session_id: str) -> SessionRecord | None: ...
    async def delete(self, session_id: str) -> None: ...
