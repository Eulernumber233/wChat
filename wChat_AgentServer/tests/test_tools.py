from __future__ import annotations

import pytest

from app.tools.chat_context import (
    GetChatHistoryTool,
    GetFriendProfileTool,
    GetRelationshipSummaryTool,
)
from app.tools.search import SearchPastSimilarTool


@pytest.mark.asyncio
async def test_get_chat_history_returns_fixture_rows(mock_backend):
    tool = GetChatHistoryTool(mock_backend, self_uid=1001, peer_uid=2002)
    r = await tool.run(limit=10)
    assert r.ok
    assert len(r.data) == 3
    assert r.data[0]["from_uid"] in (1001, 2002)


@pytest.mark.asyncio
async def test_get_chat_history_respects_limit(mock_backend):
    tool = GetChatHistoryTool(mock_backend, self_uid=1001, peer_uid=2002)
    r = await tool.run(limit=2)
    assert len(r.data) == 2
    # oldest trimmed: we keep the tail
    assert r.data[-1]["msg_db_id"] == "103"


@pytest.mark.asyncio
async def test_get_friend_profile_found(mock_backend):
    tool = GetFriendProfileTool(mock_backend, peer_uid=2002)
    r = await tool.run()
    assert r.ok
    assert r.data["name"] == "zhangsan"


@pytest.mark.asyncio
async def test_get_friend_profile_missing(mock_backend):
    tool = GetFriendProfileTool(mock_backend, peer_uid=99999)
    r = await tool.run()
    assert not r.ok
    assert r.error == "profile_not_found"


@pytest.mark.asyncio
async def test_relationship_summary_stub(mock_backend):
    tool = GetRelationshipSummaryTool(mock_backend, self_uid=1001, peer_uid=2002)
    r = await tool.run()
    assert r.ok
    assert "近期交互" in r.data


@pytest.mark.asyncio
async def test_search_disabled_returns_empty():
    tool = SearchPastSimilarTool(enabled=False)
    r = await tool.run(query="anything")
    assert r.ok
    assert r.data == []


def test_tool_openai_schema_shape(mock_backend):
    from app.tools.base import ToolRegistry

    reg = ToolRegistry()
    reg.register(GetChatHistoryTool(mock_backend, 1, 2))
    schemas = reg.openai_schemas()
    assert schemas[0]["type"] == "function"
    assert schemas[0]["function"]["name"] == "get_chat_history"
    assert "parameters" in schemas[0]["function"]
