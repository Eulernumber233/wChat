"""RedisSessionStore round-trip + TTL behavior using fakeredis."""
from __future__ import annotations

import pytest

from app.memory.base import SessionRecord
from app.memory.redis_store import RedisSessionStore


@pytest.fixture
def store(fake_redis):
    return RedisSessionStore(
        redis=fake_redis,
        ttl_seconds=60,
        key_template="agent_session:{sid}",
    )


async def test_put_then_get_round_trip(store, fake_redis):
    rec = SessionRecord(
        session_id="abc",
        self_uid=1001,
        peer_uid=2002,
        state_snapshot={"candidates": [{"index": 0, "style": "s", "content": "c"}]},
        created_at=1713150000.0,
    )
    await store.put(rec)
    got = await store.get("abc")
    assert got is not None
    assert got.session_id == "abc"
    assert got.self_uid == 1001
    assert got.peer_uid == 2002
    assert got.state_snapshot["candidates"][0]["content"] == "c"


async def test_get_missing_returns_none(store):
    assert await store.get("nonexistent") is None


async def test_ttl_is_set(store, fake_redis):
    rec = SessionRecord(session_id="ttl_check", self_uid=1, peer_uid=2)
    await store.put(rec)
    ttl = await fake_redis.ttl("agent_session:ttl_check")
    # fakeredis returns positive int when a TTL is set
    assert 0 < ttl <= 60


async def test_delete_removes_key(store, fake_redis):
    rec = SessionRecord(session_id="del_me", self_uid=1, peer_uid=2)
    await store.put(rec)
    await store.delete("del_me")
    assert await fake_redis.get("agent_session:del_me") is None


async def test_forward_compat_missing_fields(store, fake_redis):
    # simulate an older snapshot with only session_id + self_uid
    import json

    await fake_redis.set("agent_session:legacy", json.dumps({"session_id": "legacy", "self_uid": 42}))
    got = await store.get("legacy")
    assert got is not None
    assert got.self_uid == 42
    assert got.peer_uid == 0
    assert got.state_snapshot == {}
