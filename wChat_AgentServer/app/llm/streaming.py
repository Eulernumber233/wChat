"""Utilities for turning LLMChunk streams into SSE payloads.

Kept minimal — the HTTP SSE route itself is a stub this milestone.
"""
from __future__ import annotations

import json
from typing import AsyncIterator

from ..schemas.response import StreamEvent
from .base import LLMChunk


async def chunks_to_sse(
    chunks: AsyncIterator[LLMChunk],
    event_name: str = "candidate_delta",
) -> AsyncIterator[str]:
    async for chunk in chunks:
        if chunk.delta:
            evt = StreamEvent(event=event_name, data={"delta": chunk.delta})
            yield f"event: {evt.event}\ndata: {json.dumps(evt.data, ensure_ascii=False)}\n\n"
        if chunk.finish_reason:
            evt = StreamEvent(event="done", data={"finish_reason": chunk.finish_reason})
            yield f"event: {evt.event}\ndata: {json.dumps(evt.data, ensure_ascii=False)}\n\n"
