"""Request schemas for the HTTP API."""
from __future__ import annotations

from pydantic import BaseModel, Field

from .chat import TextChatData


class SuggestReplyRequest(BaseModel):
    """Primary entry point. Client sends last N messages from LocalDb::LoadRecent."""

    self_uid: int
    peer_uid: int
    recent_messages: list[TextChatData] = Field(
        description="Ordered oldest->newest. Client-supplied window, server may fetch more via tools.",
    )
    preset_id: str | None = Field(
        default=None, description="Preset id from presets.yaml; ignored if custom_prompt set"
    )
    custom_prompt: str | None = Field(
        default=None, description="User-provided scenario prompt; overrides/augments preset"
    )
    num_candidates: int = Field(default=3, ge=1, le=5)
    session_id: str | None = Field(
        default=None,
        description="If provided, server resumes an existing agent session for follow-up refinement",
    )


class RefineRequest(BaseModel):
    """Regenerate a single candidate with a user instruction."""

    session_id: str
    candidate_index: int = Field(ge=0)
    instruction: str = Field(description="e.g. '更简短些' / '语气更委婉'")
