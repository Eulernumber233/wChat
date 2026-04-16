"""Chat-context tools.

These are what the agent calls to enrich the short window the client
supplied. In production they hit ChatServer via gRPC; in dev/test they
go through MockBackend. The `backend` Protocol keeps both paths honest.
"""
from __future__ import annotations

from typing import Any, Protocol

from ..schemas.chat import TextChatData, UserProfile
from .base import ToolResult

# 这两个async函数在哪里实现的 TODO
class ChatBackend(Protocol):
    async def fetch_history(
        self, self_uid: int, peer_uid: int, limit: int, before_msg_db_id: int = 0
    ) -> list[TextChatData]: ...

    async def fetch_profile(self, peer_uid: int) -> UserProfile | None: ...


class GetChatHistoryTool:
    name = "get_chat_history"
    description = (
        "Fetch extended chat history with the peer. Use when the recent_messages "
        "window looks insufficient (e.g. the current message clearly references "
        "an older topic) or when you need more turns for style calibration."
    )
    parameters = {
        "type": "object",
        "properties": {
            "limit": {
                "type": "integer",
                "description": "Max number of messages to return (1-100)",
                "minimum": 1,
                "maximum": 100,
            },
            "before_msg_db_id": {
                "type": "integer",
                "description": "Pagination cursor; 0 means latest",
                "default": 0,
            },
        },
        "required": ["limit"],
    }

    def __init__(self, backend: ChatBackend, self_uid: int, peer_uid: int) -> None:
        self._backend = backend
        self._self_uid = self_uid
        self._peer_uid = peer_uid

    async def run(self, **kwargs: Any) -> ToolResult:
        limit = int(kwargs.get("limit", 30))
        before = int(kwargs.get("before_msg_db_id", 0))
        msgs = await self._backend.fetch_history(
            self._self_uid, self._peer_uid, limit, before
        )
        return ToolResult(ok=True, data=[m.model_dump() for m in msgs])


class GetFriendProfileTool:
    name = "get_friend_profile"
    description = (
        "Fetch the peer's base profile (name, nick, desc). Use when the reply "
        "style depends on who the peer is (colleague vs close friend vs family)."
    )
    parameters = {"type": "object", "properties": {}}

    def __init__(self, backend: ChatBackend, peer_uid: int) -> None:
        self._backend = backend
        self._peer_uid = peer_uid

    async def run(self, **kwargs: Any) -> ToolResult:
        prof = await self._backend.fetch_profile(self._peer_uid)
        if prof is None:
            return ToolResult(ok=False, data=None, error="profile_not_found")
        return ToolResult(ok=True, data=prof.model_dump())


class GetRelationshipSummaryTool:
    name = "get_relationship_summary"
    description = (
        "Return a short summary of the overall relationship and recent topics "
        "between self and peer. Cached per (self_uid, peer_uid). Use sparingly "
        "— only when recent messages lack context."
    )
    parameters = {"type": "object", "properties": {}}

    def __init__(self, backend: ChatBackend, self_uid: int, peer_uid: int) -> None:
        self._backend = backend
        self._self_uid = self_uid
        self._peer_uid = peer_uid
        # TODO: wire a real summarizer once Redis cache + background task exist.
        # For now, synthesize a stub summary from whatever history exists.

    async def run(self, **kwargs: Any) -> ToolResult:
        history = await self._backend.fetch_history(
            self._self_uid, self._peer_uid, limit=100
        )
        if not history:
            return ToolResult(ok=True, data="无历史交互记录")
        summary = (
            f"近期交互 {len(history)} 条消息,"
            f"最早时间戳 {history[0].send_time},最近 {history[-1].send_time}。"
            "(注:本期为 stub,未来接入 LLM 预生成的关系总结)"
        )
        return ToolResult(ok=True, data=summary)
