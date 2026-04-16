"""FastAPI dependency injection glue.

One place to wire the AgentService so routes stay dumb. Replaced by
overrides in tests (`app.dependency_overrides`).
"""
from __future__ import annotations

from functools import lru_cache

from ..agent.graph import AgentService
from ..config.settings import get_settings
from ..llm.deepseek import DeepSeekProvider
from ..memory.in_memory import get_memory_store
from ..tools.base import ToolRegistry
from ..tools.chat_context import (
    GetChatHistoryTool,
    GetFriendProfileTool,
    GetRelationshipSummaryTool,
)
from ..tools.mock_backend import MockBackend
from ..tools.search import SearchPastSimilarTool


@lru_cache(maxsize=1)
def _backend() -> MockBackend:
    s = get_settings()
    # TODO(milestone-2): if s.backend.mode == "grpc", return GrpcAgentDataClient
    return MockBackend(fixture_path=s.backend.fixture_path)


def _tool_factory(self_uid: int, peer_uid: int) -> ToolRegistry:
    reg = ToolRegistry()
    backend = _backend()  # ← MockBackend 单例,懒加载
    reg.register(GetChatHistoryTool(backend, self_uid, peer_uid))
    reg.register(GetFriendProfileTool(backend, peer_uid))
    reg.register(GetRelationshipSummaryTool(backend, self_uid, peer_uid))
    reg.register(SearchPastSimilarTool(enabled=get_settings().agent.enable_rag))
    return reg


@lru_cache(maxsize=1) # 整个进程只初始化一次
def _agent_service() -> AgentService:
    s = get_settings()
    llm = DeepSeekProvider(
        api_key=s.llm.api_key,
        base_url=s.llm.base_url,
        model=s.llm.model,
    )
    return AgentService(llm=llm, tool_factory=_tool_factory, memory=get_memory_store())


def get_agent_service() -> AgentService:
    return _agent_service()


def reset_deps() -> None:
    """Test helper: clear cached backend/service so reconfiguration takes effect."""
    _backend.cache_clear()
    _agent_service.cache_clear()
