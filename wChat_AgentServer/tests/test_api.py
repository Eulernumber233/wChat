"""Route-level tests using FastAPI TestClient + dependency overrides.

We override get_agent_service so routes use FakeLLM end-to-end without
hitting DeepSeek. Also swaps the global preset path via direct override.
"""
from __future__ import annotations

from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.api.deps import get_agent_service
from app.config.settings import get_settings
from app.main import create_app
from tests.conftest import (
    FakeLLM,
    build_agent_service,
    make_generate_response,
    make_intent_response,
)

TEST_UID = 1001
TEST_TOKEN = "test-token-abc"
AUTH_HEADERS = {
    "Authorization": f"Bearer {TEST_TOKEN}",
    "X-Self-Uid": str(TEST_UID),
}


@pytest.fixture
def client(mock_backend, fake_redis, monkeypatch):
    # Point the preset loader at the repo's presets.yaml
    from app import presets as presets_pkg

    presets_path = Path(__file__).parent.parent / "config" / "presets.yaml"
    monkeypatch.setattr(
        presets_pkg.loader,
        "_cached",
        presets_pkg.loader.load_presets(presets_path),
    )

    # Seed the token AgentServer will look up on every authed call.
    # fakeredis exposes a sync client under the same instance, but the
    # simplest path is a one-shot asyncio.run().
    import asyncio

    async def _seed():
        key = get_settings().auth.token_key_template.format(uid=TEST_UID)
        await fake_redis.set(key, TEST_TOKEN)

    asyncio.run(_seed())

    llm = FakeLLM(
        [
            make_intent_response(),
            make_generate_response(num_candidates=3),
        ]
    )
    agent = build_agent_service(llm, mock_backend)

    app = create_app()
    app.dependency_overrides[get_agent_service] = lambda: agent
    return TestClient(app)


def test_health(client):
    r = client.get("/agent/health")
    assert r.status_code == 200
    assert r.json() == {"status": "ok"}


def test_list_presets(client):
    r = client.get("/agent/presets")
    assert r.status_code == 200
    body = r.json()
    assert any(p["id"] == "polite_decline" for p in body)


def test_suggest_reply_route(client):
    payload = {
        "self_uid": TEST_UID,
        "peer_uid": 2002,
        "recent_messages": [
            {
                "msg_db_id": "103",
                "from_uid": 2002,
                "to_uid": TEST_UID,
                "msg_type": 1,
                "content": "能借我五千吗",
                "send_time": 1713150080,
                "direction": 0,
            }
        ],
        "preset_id": "polite_decline",
        "num_candidates": 3,
    }
    r = client.post("/agent/suggest_reply", json=payload, headers=AUTH_HEADERS)
    assert r.status_code == 200, r.text
    body = r.json()
    assert len(body["candidates"]) == 3
    assert body["session_id"]


def test_suggest_reply_empty_messages_rejected(client):
    payload = {
        "self_uid": TEST_UID,
        "peer_uid": 2002,
        "recent_messages": [],
        "num_candidates": 3,
    }
    r = client.post("/agent/suggest_reply", json=payload, headers=AUTH_HEADERS)
    assert r.status_code == 400


def test_suggest_reply_unknown_preset_404(client):
    payload = {
        "self_uid": TEST_UID,
        "peer_uid": 2002,
        "recent_messages": [
            {
                "msg_db_id": "1",
                "from_uid": 2002,
                "to_uid": TEST_UID,
                "msg_type": 1,
                "content": "hi",
                "send_time": 1,
                "direction": 0,
            }
        ],
        "preset_id": "no_such_preset",
        "num_candidates": 3,
    }
    r = client.post("/agent/suggest_reply", json=payload, headers=AUTH_HEADERS)
    assert r.status_code == 404


def test_stream_endpoint_returns_sse(client):
    payload = {
        "self_uid": TEST_UID,
        "peer_uid": 2002,
        "recent_messages": [
            {
                "msg_db_id": "103",
                "from_uid": 2002,
                "to_uid": TEST_UID,
                "msg_type": 1,
                "content": "能借我五千吗",
                "send_time": 1713150080,
                "direction": 0,
            }
        ],
        "preset_id": "polite_decline",
        "num_candidates": 3,
    }
    r = client.post(
        "/agent/suggest_reply/stream", json=payload, headers=AUTH_HEADERS,
    )
    assert r.status_code == 200, r.text
    assert r.headers["content-type"].startswith("text/event-stream")
    body = r.text
    assert "event: intent" in body
    assert "event: candidate_done" in body or "event: candidate_delta" in body
    assert "event: done" in body


def test_suggest_reply_missing_auth_401(client):
    payload = {
        "self_uid": TEST_UID,
        "peer_uid": 2002,
        "recent_messages": [
            {
                "msg_db_id": "1",
                "from_uid": 2002,
                "to_uid": TEST_UID,
                "msg_type": 1,
                "content": "hi",
                "send_time": 1,
                "direction": 0,
            }
        ],
        "preset_id": "polite_decline",
        "num_candidates": 3,
    }
    # no headers → 401
    r = client.post("/agent/suggest_reply", json=payload)
    assert r.status_code == 401


def test_suggest_reply_bad_token_401(client):
    payload = {
        "self_uid": TEST_UID,
        "peer_uid": 2002,
        "recent_messages": [
            {
                "msg_db_id": "1",
                "from_uid": 2002,
                "to_uid": TEST_UID,
                "msg_type": 1,
                "content": "hi",
                "send_time": 1,
                "direction": 0,
            }
        ],
        "num_candidates": 3,
    }
    headers = {**AUTH_HEADERS, "Authorization": "Bearer wrong-token"}
    r = client.post("/agent/suggest_reply", json=payload, headers=headers)
    assert r.status_code == 401


def test_suggest_reply_uid_mismatch_403(client):
    payload = {
        "self_uid": 9999,  # body says 9999, header says TEST_UID
        "peer_uid": 2002,
        "recent_messages": [
            {
                "msg_db_id": "1",
                "from_uid": 2002,
                "to_uid": 9999,
                "msg_type": 1,
                "content": "hi",
                "send_time": 1,
                "direction": 0,
            }
        ],
        "num_candidates": 3,
    }
    r = client.post("/agent/suggest_reply", json=payload, headers=AUTH_HEADERS)
    assert r.status_code == 403
