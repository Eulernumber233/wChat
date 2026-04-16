"""End-to-end agent graph tests using FakeLLM + MockBackend fixtures."""
from __future__ import annotations

import pytest

from app.schemas.chat import TextChatData
from app.schemas.request import RefineRequest, SuggestReplyRequest
from tests.conftest import (
    FakeLLM,
    build_agent_service,
    make_generate_response,
    make_intent_response,
)


def _recent_messages(backend, self_uid: int, peer_uid: int) -> list[TextChatData]:
    scen = backend._fixtures[(self_uid, peer_uid)]
    return [TextChatData(**m) for m in scen["messages"]]


@pytest.mark.asyncio
async def test_suggest_reply_happy_path_no_tools(mock_backend, preset_store):
    llm = FakeLLM(
        [
            make_intent_response(needs_tools=[]),
            make_generate_response(num_candidates=3),
        ]
    )
    agent = build_agent_service(llm, mock_backend)

    req = SuggestReplyRequest(
        self_uid=1001,
        peer_uid=2002,
        recent_messages=_recent_messages(mock_backend, 1001, 2002),
        preset_id="polite_decline",
        num_candidates=3,
    )
    resp = await agent.suggest_reply(req, preset_store.get("polite_decline"))

    assert len(resp.candidates) == 3
    assert all(c.content for c in resp.candidates)
    assert [c.index for c in resp.candidates] == [0, 1, 2]
    assert resp.intent_analysis
    assert resp.strategy
    assert resp.session_id
    assert resp.tokens_used > 0
    assert len(llm.calls) == 2


@pytest.mark.asyncio
async def test_suggest_reply_invokes_all_requested_tools(mock_backend, preset_store):
    llm = FakeLLM(
        [
            make_intent_response(
                needs_tools=[
                    "get_friend_profile",
                    "get_chat_history",
                    "get_relationship_summary",
                ]
            ),
            make_generate_response(num_candidates=2),
        ]
    )
    agent = build_agent_service(llm, mock_backend)
    req = SuggestReplyRequest(
        self_uid=1001,
        peer_uid=2002,
        recent_messages=_recent_messages(mock_backend, 1001, 2002),
        num_candidates=2,
    )
    resp = await agent.suggest_reply(req, None)

    assert set(resp.tools_used) == {
        "get_friend_profile",
        "get_chat_history",
        "get_relationship_summary",
    }
    assert len(resp.candidates) == 2


@pytest.mark.asyncio
async def test_suggest_reply_pads_when_llm_returns_fewer_candidates(
    mock_backend, preset_store
):
    # LLM returns 1 candidate but we asked for 3 → expect padding with placeholders
    llm = FakeLLM(
        [
            make_intent_response(),
            make_generate_response(num_candidates=1),
        ]
    )
    agent = build_agent_service(llm, mock_backend)
    req = SuggestReplyRequest(
        self_uid=1001,
        peer_uid=3003,
        recent_messages=_recent_messages(mock_backend, 1001, 3003),
        preset_id="comfort",
        num_candidates=3,
    )
    resp = await agent.suggest_reply(req, preset_store.get("comfort"))
    assert len(resp.candidates) == 3
    assert resp.candidates[0].content == "候选回复0的正文"
    # padded slots
    assert "生成失败" in resp.candidates[2].content


@pytest.mark.asyncio
async def test_refine_regenerates_single_candidate(mock_backend, preset_store):
    import json

    refined = json.dumps(
        {"style": "更简短", "content": "嗯,周末可能有安排,改天再约", "reasoning": "简化"}
    )
    llm = FakeLLM(
        [
            make_intent_response(),
            make_generate_response(num_candidates=3),
            refined,
        ]
    )
    agent = build_agent_service(llm, mock_backend)
    req = SuggestReplyRequest(
        self_uid=1001,
        peer_uid=4004,
        recent_messages=_recent_messages(mock_backend, 1001, 4004),
        preset_id="polite_decline",
        num_candidates=3,
    )
    first = await agent.suggest_reply(req, preset_store.get("polite_decline"))
    refined_cand = await agent.refine(
        RefineRequest(
            session_id=first.session_id,
            candidate_index=1,
            instruction="更简短些",
        )
    )
    assert refined_cand.index == 1
    assert "改天再约" in refined_cand.content


@pytest.mark.asyncio
async def test_refine_raises_on_missing_session(mock_backend):
    llm = FakeLLM([make_intent_response(), make_generate_response()])
    agent = build_agent_service(llm, mock_backend)
    with pytest.raises(KeyError):
        await agent.refine(
            RefineRequest(session_id="nonexistent", candidate_index=0, instruction="x")
        )


@pytest.mark.asyncio
async def test_suggest_reply_tolerates_bad_json_from_llm(mock_backend):
    llm = FakeLLM(
        [
            make_intent_response(),
            "this is not json at all, LLM went rogue",
        ]
    )
    agent = build_agent_service(llm, mock_backend)
    req = SuggestReplyRequest(
        self_uid=1001,
        peer_uid=2002,
        recent_messages=_recent_messages(mock_backend, 1001, 2002),
        num_candidates=3,
    )
    resp = await agent.suggest_reply(req, None)
    # graph should not crash; it pads placeholders
    assert len(resp.candidates) == 3
    assert all("生成失败" in c.content for c in resp.candidates)
