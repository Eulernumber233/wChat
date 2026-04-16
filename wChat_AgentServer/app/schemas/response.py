"""Response schemas."""
from __future__ import annotations

from typing import Any

from pydantic import BaseModel, Field


class Candidate(BaseModel):
    index: int
    style: str = Field(description="Short label, e.g. '委婉拒绝' / '简短确认'")
    content: str
    reasoning: str | None = Field(default=None, description="Why the agent picked this angle")


class SuggestReplyResponse(BaseModel):
    session_id: str = Field(description="Server-generated session id for follow-up refine requests")
    candidates: list[Candidate]
    intent_analysis: str = Field(description="Agent's read on what the peer is expressing")
    strategy: str = Field(description="Overall response strategy the agent chose")
    tokens_used: int = 0
    tools_used: list[str] = Field(default_factory=list)


class StreamEvent(BaseModel):
    """SSE payload format. Reserved for future streaming endpoint."""

    event: str = Field(description="'intent' | 'candidate_delta' | 'candidate_done' | 'done' | 'error'")
    data: dict[str, Any]
