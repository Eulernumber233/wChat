"""Route-level tests using FastAPI TestClient + dependency overrides.

We override get_agent_service so routes use FakeLLM end-to-end without
hitting DeepSeek. Also swaps the global preset path via direct override.
"""
from __future__ import annotations

from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.api.deps import get_agent_service
from app.main import create_app
from tests.conftest import (
    FakeLLM,
    build_agent_service,
    make_generate_response,
    make_intent_response,
)


@pytest.fixture
def client(mock_backend, monkeypatch):
    # Point the preset loader at the repo's presets.yaml
    from app import presets as presets_pkg

    presets_path = Path(__file__).parent.parent / "config" / "presets.yaml"
    monkeypatch.setattr(
        presets_pkg.loader,
        "_cached",
        presets_pkg.loader.load_presets(presets_path),
    )

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
        "self_uid": 1001,
        "peer_uid": 2002,
        "recent_messages": [
            {
                "msg_db_id": "103",
                "from_uid": 2002,
                "to_uid": 1001,
                "msg_type": 1,
                "content": "能借我五千吗",
                "send_time": 1713150080,
                "direction": 0,
            }
        ],
        "preset_id": "polite_decline",
        "num_candidates": 3,
    }
    r = client.post("/agent/suggest_reply", json=payload)
    assert r.status_code == 200, r.text
    body = r.json()
    assert len(body["candidates"]) == 3
    assert body["session_id"]


def test_suggest_reply_empty_messages_rejected(client):
    payload = {
        "self_uid": 1001,
        "peer_uid": 2002,
        "recent_messages": [],
        "num_candidates": 3,
    }
    r = client.post("/agent/suggest_reply", json=payload)
    assert r.status_code == 400


def test_suggest_reply_unknown_preset_404(client):
    payload = {
        "self_uid": 1001,
        "peer_uid": 2002,
        "recent_messages": [
            {
                "msg_db_id": "1",
                "from_uid": 2002,
                "to_uid": 1001,
                "msg_type": 1,
                "content": "hi",
                "send_time": 1,
                "direction": 0,
            }
        ],
        "preset_id": "no_such_preset",
        "num_candidates": 3,
    }
    r = client.post("/agent/suggest_reply", json=payload)
    assert r.status_code == 404


def test_stream_endpoint_returns_501(client):
    r = client.get("/agent/suggest_reply/stream")
    assert r.status_code == 501
