"""gRPC client for ChatServer's AgentDataService.

Implements the `AgentDataClient` Protocol so it's a drop-in for
`MockBackend` at the `ChatBackend` seam used by tools.

Connection strategy:
  - Single grpc.aio channel per process, lazily created on first call.
  - Multi-target support: `settings.backend.grpc_targets` is a
    comma-separated list; we round-robin among them at channel
    creation. If multiple ChatServer instances register the same
    AgentDataService (they do), any of them can answer.
  - Retries: we don't add app-level retry; grpc has native retries for
    UNAVAILABLE/DEADLINE_EXCEEDED, and a failed Agent call just means
    the LLM gets less context — not worth masking transient faults.

Token handling:
  - We pass `auth_token` as a request-body field (matches proto).
    See CLAUDE.md / rpc/README.md for why this is preferred over
    gRPC metadata here.

msg_db_id conversion:
  - proto field is int64; we cast to str when building TextChatData
    because wire representation back out to HTTP goes through jsoncpp
    on the client, which can't round-trip int64 (see schemas/chat.py).
"""
from __future__ import annotations

import asyncio
import itertools
import logging
from typing import TYPE_CHECKING

import grpc

from ..config.settings import get_settings
from ..schemas.chat import TextChatData, UserProfile

if TYPE_CHECKING:  # avoid mandatory grpc-stub import at module load
    from .gen import message_pb2, message_pb2_grpc  # noqa: F401

log = logging.getLogger(__name__)


def _parse_targets(raw: str) -> list[str]:
    out = [t.strip() for t in raw.split(",") if t.strip()]
    if not out:
        raise ValueError("backend.grpc_targets is empty — set at least one host:port")
    return out


class GrpcAgentDataClient:
    """Async gRPC client.

    Thread / asyncio safety: a grpc.aio.Channel is safe for concurrent
    use across tasks. We create channels lazily and cache them for the
    life of the process.
    """

    def __init__(self, targets: list[str] | None = None) -> None:
        if targets is None:
            targets = _parse_targets(get_settings().backend.grpc_targets)
        self._targets = targets
        self._channel: grpc.aio.Channel | None = None
        self._stub = None  # type: ignore[assignment]
        self._pick = itertools.cycle(self._targets) # 无限循环的迭代器，用于轮询
        self._lock = asyncio.Lock()

    async def _ensure_channel(self):
        if self._channel is not None and self._stub is not None:
            return self._stub
        async with self._lock:
            if self._channel is not None and self._stub is not None:
                return self._stub
            # import lazily so tests that never use grpc don't pay the
            # descriptor-load cost; also keeps gen/ optional at runtime
            from .gen import message_pb2_grpc

            target = next(self._pick)
            log.info("creating grpc.aio channel to %s", target)
            self._channel = grpc.aio.insecure_channel(target)
            self._stub = message_pb2_grpc.AgentDataServiceStub(self._channel)
            return self._stub

    async def close(self) -> None:
        if self._channel is not None:
            await self._channel.close()
            self._channel = None
            self._stub = None

    # ------------------------------------------------------------------ #
    # AgentDataClient Protocol                                           #
    # ------------------------------------------------------------------ #
    async def fetch_history(
        self,
        self_uid: int,
        peer_uid: int,
        limit: int,
        before_msg_db_id: int = 0,
        *,
        auth_token: str,
    ) -> list[TextChatData]:
        stub = await self._ensure_channel()
        from .gen import message_pb2  # lazy — see __init__

        req = message_pb2.GetChatHistoryReq(
            self_uid=self_uid,
            peer_uid=peer_uid,
            limit=limit,
            before_msg_db_id=before_msg_db_id,
            auth_token=auth_token, # token 放在请求体字段中，而非 gRPC metadata
        )
        try:
            rsp = await stub.GetChatHistory(req)
        except grpc.aio.AioRpcError as e:
            log.warning("GetChatHistory grpc error: %s — %s", e.code(), e.details())
            return []

        if rsp.error != 0:
            log.warning(
                "GetChatHistory returned error=%d self=%d peer=%d",
                rsp.error, self_uid, peer_uid,
            )
            return []

        out: list[TextChatData] = []
        for row in rsp.messages:
            try:
                out.append(
                    TextChatData(
                        msg_db_id=str(row.msg_db_id),
                        from_uid=row.from_uid,
                        to_uid=row.to_uid,
                        msg_type=row.msg_type,
                        content=row.content,
                        send_time=row.send_time,
                        direction=row.direction,
                    )
                )
            except Exception as exc:  # noqa: BLE001
                # one bad row shouldn't kill the whole fetch — log + skip
                log.warning("skipping malformed row msg_db_id=%s err=%s",
                            row.msg_db_id, exc)

        # proto returns id-DESC (newest first). Our downstream code treats
        # the list as oldest→newest (see nodes.py _profile_block / prompts.
        # format_messages), so reverse.
        out.reverse()
        return out

    async def fetch_profile(
        self, self_uid: int, peer_uid: int, *, auth_token: str
    ) -> UserProfile | None:
        stub = await self._ensure_channel()
        from .gen import message_pb2

        req = message_pb2.GetFriendProfileReq(
            self_uid=self_uid,
            peer_uid=peer_uid,
            auth_token=auth_token,
        )
        try:
            rsp = await stub.GetFriendProfile(req)
        except grpc.aio.AioRpcError as e:
            log.warning("GetFriendProfile grpc error: %s — %s", e.code(), e.details())
            return None

        if rsp.error != 0:
            log.info(
                "GetFriendProfile returned error=%d peer=%d — treating as not found",
                rsp.error, peer_uid,
            )
            return None
        return UserProfile(
            uid=rsp.uid,
            name=rsp.name,
            nick=rsp.nick or None,
            sex=rsp.sex,
            desc=rsp.desc or None,
            icon=rsp.icon or None,
        )
