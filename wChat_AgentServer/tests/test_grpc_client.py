"""End-to-end test of GrpcAgentDataClient against a Python in-process
AgentDataService implementation.

This is the closest thing to a full-stack test we can run without the
C++ ChatServer: we stand up the same service contract (generated from
the shared proto) with a fake implementation, point our client at it,
and assert the wire-level round-trip.

Why this exists:
  - Proves the generated stubs are usable end-to-end (catches proto
    drift that unit tests on MockBackend miss).
  - Validates our field mapping: int64 msg_db_id → str in TextChatData,
    row DESC → oldest-first reversal, proto None-ish fields → Pydantic
    Optional[...] = None.
"""
from __future__ import annotations

import pytest
import grpc

from app.rpc.agent_data_client import GrpcAgentDataClient
from app.rpc.gen import message_pb2, message_pb2_grpc

pytestmark = pytest.mark.asyncio


class FakeAgentDataService(message_pb2_grpc.AgentDataServiceServicer):
    """Stand-in for ChatServer's AgentDataServiceImpl."""

    def __init__(self) -> None:
        self.history_calls: list[tuple[int, int, int, int, str]] = []
        self.profile_calls: list[tuple[int, int, str]] = []
        self.history_rows: list[message_pb2.AgentChatMessageRow] = []
        self.history_error = 0
        self.profile_error = 0
        self.profile_payload: dict[str, object] = {}

    async def GetChatHistory(self, request, context):
        self.history_calls.append((
            request.self_uid, request.peer_uid, request.limit,
            request.before_msg_db_id, request.auth_token,
        ))
        rsp = message_pb2.GetChatHistoryRsp()
        rsp.error = self.history_error
        for r in self.history_rows:
            rsp.messages.append(r)
        return rsp

    async def GetFriendProfile(self, request, context):
        self.profile_calls.append((
            request.self_uid, request.peer_uid, request.auth_token,
        ))
        rsp = message_pb2.GetFriendProfileRsp()
        rsp.error = self.profile_error
        if self.profile_error == 0 and self.profile_payload:
            p = self.profile_payload
            rsp.uid = int(p.get("uid", 0))
            rsp.name = str(p.get("name", ""))
            rsp.nick = str(p.get("nick", ""))
            rsp.sex = int(p.get("sex", 0))
            rsp.desc = str(p.get("desc", ""))
            rsp.icon = str(p.get("icon", ""))
        return rsp


@pytest.fixture
async def grpc_harness():
    """Start an aio grpc server on an ephemeral port; yield (addr, svc)."""
    server = grpc.aio.server()
    svc = FakeAgentDataService()
    message_pb2_grpc.add_AgentDataServiceServicer_to_server(svc, server)
    port = server.add_insecure_port("127.0.0.1:0")
    await server.start()
    try:
        yield f"127.0.0.1:{port}", svc
    finally:
        await server.stop(grace=0)


async def test_fetch_history_happy_path(grpc_harness):
    addr, svc = grpc_harness
    # server-side emits rows in id-DESC (newest first), as ChatServer does
    svc.history_rows = [
        message_pb2.AgentChatMessageRow(
            msg_db_id=103, from_uid=2002, to_uid=1001,
            msg_type=1, content="能借我五千吗", send_time=1713150080, direction=0,
        ),
        message_pb2.AgentChatMessageRow(
            msg_db_id=102, from_uid=1001, to_uid=2002,
            msg_type=1, content="在的 咋了", send_time=1713150030, direction=1,
        ),
    ]
    client = GrpcAgentDataClient(targets=[addr])
    try:
        msgs = await client.fetch_history(1001, 2002, limit=30, auth_token="t-xyz")
    finally:
        await client.close()

    assert len(msgs) == 2
    # oldest→newest after client-side reverse
    assert msgs[0].msg_db_id == "102"
    assert msgs[1].msg_db_id == "103"
    # msg_db_id survived int64→str conversion
    assert isinstance(msgs[0].msg_db_id, str)
    # direction passed through
    assert msgs[0].direction.value == 1
    assert msgs[1].direction.value == 0
    # token reached the server
    assert svc.history_calls[-1][4] == "t-xyz"


async def test_fetch_history_server_error_yields_empty(grpc_harness):
    addr, svc = grpc_harness
    svc.history_error = 1010  # TokenInvalid on ChatServer side
    client = GrpcAgentDataClient(targets=[addr])
    try:
        msgs = await client.fetch_history(1001, 2002, limit=10, auth_token="")
    finally:
        await client.close()
    assert msgs == []


async def test_fetch_history_grpc_unreachable_yields_empty():
    # no server at this port → AioRpcError caught, returns []
    client = GrpcAgentDataClient(targets=["127.0.0.1:1"])
    try:
        msgs = await client.fetch_history(1, 2, limit=5, auth_token="t")
    finally:
        await client.close()
    assert msgs == []


async def test_fetch_profile_happy_path(grpc_harness):
    addr, svc = grpc_harness
    svc.profile_payload = {
        "uid": 2002, "name": "zhangsan", "nick": "老张",
        "sex": 1, "desc": "朋友", "icon": "a.png",
    }
    client = GrpcAgentDataClient(targets=[addr])
    try:
        prof = await client.fetch_profile(1001, 2002, auth_token="t")
    finally:
        await client.close()

    assert prof is not None
    assert prof.uid == 2002
    assert prof.name == "zhangsan"
    assert prof.nick == "老张"
    assert prof.desc == "朋友"
    assert svc.profile_calls[-1] == (1001, 2002, "t")


async def test_fetch_profile_not_found(grpc_harness):
    addr, svc = grpc_harness
    svc.profile_error = 1011  # UidInvalid
    client = GrpcAgentDataClient(targets=[addr])
    try:
        prof = await client.fetch_profile(1001, 99999, auth_token="t")
    finally:
        await client.close()
    assert prof is None


async def test_empty_optional_fields_become_none(grpc_harness):
    addr, svc = grpc_harness
    # only required fields; nick/desc/icon are empty strings on the wire
    svc.profile_payload = {"uid": 2002, "name": "zs", "nick": "", "sex": 0, "desc": "", "icon": ""}
    client = GrpcAgentDataClient(targets=[addr])
    try:
        prof = await client.fetch_profile(1001, 2002, auth_token="t")
    finally:
        await client.close()
    assert prof is not None
    # empty strings coerced to None so downstream prompt formatting uses "-"
    assert prof.nick is None
    assert prof.desc is None
    assert prof.icon is None
