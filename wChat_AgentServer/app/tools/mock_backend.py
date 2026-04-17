"""Mock data backend for tests/local dev.

Replaces the future gRPC client so we can exercise the full agent loop
without a running ChatServer. Fixture format matches LocalDb::LoadRecent
row shape (see tests/fixtures/sample_chats.json).
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from ..schemas.chat import TextChatData, UserProfile


class MockBackend:
    def __init__(self, fixture_path: str | Path | None = None) -> None:
        self._fixture_path = Path(fixture_path) if fixture_path else None
        self._fixtures: dict[tuple[int, int], dict[str, Any]] = {}
        if self._fixture_path and self._fixture_path.exists():
            self._load()

    def _load(self) -> None:
        assert self._fixture_path is not None
        raw = json.loads(self._fixture_path.read_text(encoding="utf-8"))
        scenarios = raw if isinstance(raw, list) else [raw]
        for scen in scenarios:
            key = (int(scen["self_uid"]), int(scen["peer_uid"]))
            self._fixtures[key] = scen

    async def fetch_history(
        self,
        self_uid: int,
        peer_uid: int,
        limit: int,
        before_msg_db_id: int = 0,
        *,
        auth_token: str = "",
    ) -> list[TextChatData]:
        # auth_token ignored — MockBackend trusts all callers by design
        del auth_token
        scen = self._fixtures.get((self_uid, peer_uid))
        if not scen:
            return []
        msgs = [TextChatData(**m) for m in scen.get("messages", [])]
        if before_msg_db_id:
            msgs = [m for m in msgs if int(m.msg_db_id) < before_msg_db_id]
        return msgs[-limit:]

    async def fetch_profile(
        self, self_uid: int, peer_uid: int, *, auth_token: str = ""
    ) -> UserProfile | None:
        del self_uid, auth_token  # fixtures key on peer_uid alone
        for (_, p), scen in self._fixtures.items():
            if p == peer_uid and "friend_profile" in scen:
                return UserProfile(**scen["friend_profile"])
        return None

    def inject(
        self,
        self_uid: int,
        peer_uid: int,
        messages: list[TextChatData] | list[dict[str, Any]],
        friend_profile: dict[str, Any] | UserProfile | None = None,
    ) -> None:
        """Test helper: inject a scenario without a JSON file."""
        msgs = [m.model_dump() if isinstance(m, TextChatData) else m for m in messages]
        prof = (
            friend_profile.model_dump()
            if isinstance(friend_profile, UserProfile)
            else friend_profile
        )
        self._fixtures[(self_uid, peer_uid)] = {
            "self_uid": self_uid,
            "peer_uid": peer_uid,
            "messages": msgs,
            "friend_profile": prof,
        }
