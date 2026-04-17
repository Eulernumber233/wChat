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
    ChatBackend,
    GetChatHistoryTool,
    GetFriendProfileTool,
    GetRelationshipSummaryTool,
)
from ..tools.mock_backend import MockBackend
from ..tools.search import SearchPastSimilarTool


@lru_cache(maxsize=1)
def _backend() -> ChatBackend:
    """Data backend for the agent.

    `mock`: fixture-driven (tests, local dev without ChatServer)
    `grpc`: real AgentDataService on ChatServer, dispatched via
            settings.backend.grpc_targets (comma-separated).

    Both satisfy the ChatBackend Protocol (fetch_history + fetch_profile).
    """
    s = get_settings()
    if s.backend.mode == "grpc":
        # lazy import so `mock` mode doesn't pay the grpc descriptor load
        from ..rpc.agent_data_client import GrpcAgentDataClient

        return GrpcAgentDataClient()  # reads targets from settings
    return MockBackend(fixture_path=s.backend.fixture_path)


def _tool_factory(self_uid: int, peer_uid: int, auth_token: str) -> ToolRegistry:
    """Per-request tool registry.

    auth_token is forwarded to every tool so the gRPC backend can attach
    it to each RPC call (ChatServer validates against Redis utoken_<uid>).
    """
    reg = ToolRegistry()
    backend = _backend()  # backend singleton (mock or grpc); safe to share
    reg.register(GetChatHistoryTool(backend, self_uid, peer_uid, auth_token))
    reg.register(GetFriendProfileTool(backend, self_uid, peer_uid, auth_token))
    reg.register(GetRelationshipSummaryTool(backend, self_uid, peer_uid, auth_token))
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
