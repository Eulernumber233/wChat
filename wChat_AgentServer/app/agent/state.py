"""Agent state flowing through the LangGraph nodes.

TypedDict (not dataclass) because LangGraph merges partial returns from
each node into the running state, and TypedDict is its native shape.
"""
from __future__ import annotations

from typing import TypedDict

from ..presets.models import Preset
from ..schemas.chat import TextChatData, UserProfile
from ..schemas.response import Candidate


class AgentState(TypedDict, total=False):
    # --- input (populated before invoke) ---
    self_uid: int
    peer_uid: int
    recent_messages: list[TextChatData]
    preset: Preset | None
    custom_prompt: str | None
    num_candidates: int

    # --- populated by tool nodes ---
    friend_profile: UserProfile | None
    extended_history: list[TextChatData]
    relationship_summary: str | None
    tools_used: list[str]

    # --- populated by LLM nodes ---
    intent: str
    strategy: str
    candidates: list[Candidate]

    # --- metrics / diagnostics ---
    tokens_used: int
    errors: list[str]
