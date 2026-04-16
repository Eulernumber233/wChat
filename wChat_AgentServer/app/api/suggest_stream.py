"""GET /agent/suggest_reply/stream — SSE streaming variant.

Reserved. The plumbing (LLMProvider.stream + llm/streaming.chunks_to_sse)
is in place, but wiring it through the graph requires LangGraph's
streaming API which we're deferring until Milestone 2. For now this route
returns 501 Not Implemented so clients can detect unavailability.
"""
from __future__ import annotations

from fastapi import APIRouter, HTTPException

router = APIRouter(prefix="/agent", tags=["agent"])


@router.get("/suggest_reply/stream")
async def suggest_reply_stream() -> None:
    raise HTTPException(
        status_code=501,
        detail="SSE streaming is reserved for milestone 2; use POST /agent/suggest_reply",
    )
