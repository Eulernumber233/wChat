"""Shared pytest fixtures + a FakeLLM so tests don't hit DeepSeek."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any, AsyncIterator

import pytest

from app.agent.graph import AgentService
from app.llm.base import LLMChunk, LLMProvider, LLMResponse
from app.memory.in_memory import InMemoryStore
from app.presets.loader import load_presets
from app.tools.base import ToolRegistry
from app.tools.chat_context import (
    GetChatHistoryTool,
    GetFriendProfileTool,
    GetRelationshipSummaryTool,
)
from app.tools.mock_backend import MockBackend
from app.tools.search import SearchPastSimilarTool

FIXTURE_PATH = Path(__file__).parent / "fixtures" / "sample_chats.json"
PRESETS_PATH = Path(__file__).parent.parent / "config" / "presets.yaml"


class FakeLLM(LLMProvider):
    """Scripted LLM for unit tests.

    Pushes prepared JSON responses in order. If a test runs more calls
    than responses queued, the last response is reused.
    """

    def __init__(self, responses: list[str]) -> None:
        self._responses = list(responses)
        self.calls: list[list[dict[str, Any]]] = []

    async def complete(
        self,
        messages: list[dict[str, Any]],
        *,
        tools: list[dict[str, Any]] | None = None,
        response_format: dict[str, Any] | None = None,
        temperature: float = 0.7,
        max_tokens: int | None = None,
    ) -> LLMResponse:
        self.calls.append(messages)
        content = self._responses[0] if len(self._responses) == 1 else self._responses.pop(0)
        return LLMResponse(content=content, prompt_tokens=10, completion_tokens=20)

    async def stream(  # pragma: no cover - not used in unit tests
        self, *args: Any, **kwargs: Any
    ) -> AsyncIterator[LLMChunk]:
        yield LLMChunk(delta="", finish_reason="stop")


@pytest.fixture
def mock_backend() -> MockBackend:
    return MockBackend(fixture_path=FIXTURE_PATH)


@pytest.fixture
def preset_store():
    return load_presets(PRESETS_PATH)


def build_agent_service(llm: LLMProvider, backend: MockBackend) -> AgentService:
    def factory(self_uid: int, peer_uid: int) -> ToolRegistry:
        reg = ToolRegistry()
        reg.register(GetChatHistoryTool(backend, self_uid, peer_uid))
        reg.register(GetFriendProfileTool(backend, peer_uid))
        reg.register(GetRelationshipSummaryTool(backend, self_uid, peer_uid))
        reg.register(SearchPastSimilarTool(enabled=False))
        return reg

    return AgentService(llm=llm, tool_factory=factory, memory=InMemoryStore())


def make_generate_response(
    num_candidates: int = 3, strategy: str = "先共情再给出具体回应"
) -> str:
    cands = [
        {
            "index": i,
            "style": f"风格{i}",
            "content": f"候选回复{i}的正文",
            "reasoning": f"理由{i}",
        }
        for i in range(num_candidates)
    ]
    return json.dumps({"strategy": strategy, "candidates": cands}, ensure_ascii=False)


def make_intent_response(needs_tools: list[str] | None = None) -> str:
    return json.dumps(
        {"intent": "对方在借钱", "needs_tools": needs_tools or []},
        ensure_ascii=False,
    )
