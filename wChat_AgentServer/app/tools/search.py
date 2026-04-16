"""RAG placeholder.

Full implementation requires an embedding model + vector store (Qdrant/
pgvector) + an ingestion pipeline to index chat_messages. Out of scope
this milestone — we register the tool so the agent surface stays stable,
but it returns an empty hit list.
"""
from __future__ import annotations

from typing import Any

from .base import ToolResult


class SearchPastSimilarTool:
    name = "search_past_similar"
    description = (
        "Semantic search over past messages with this peer for situations "
        "similar to the current one. Returns up to topk relevant messages. "
        "Useful when you want to mirror how the user handled a similar case before."
    )
    parameters = {
        "type": "object",
        "properties": {
            "query": {"type": "string", "description": "Natural language description of the situation"},
            "topk": {"type": "integer", "minimum": 1, "maximum": 10, "default": 3},
        },
        "required": ["query"],
    }

    def __init__(self, enabled: bool = False) -> None:
        self._enabled = enabled

    async def run(self, **kwargs: Any) -> ToolResult:
        if not self._enabled:
            return ToolResult(ok=True, data=[], error="rag_disabled")
        # TODO: embed query, search vector store, dedupe, return TextChatData list
        return ToolResult(ok=True, data=[])
