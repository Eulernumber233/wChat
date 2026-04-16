"""Placeholder for the future gRPC client that will call ChatServer's
AgentDataService. NOT implemented this milestone — see rpc/README.md.

Shape is deliberately identical to MockBackend so switching from
mock → grpc is a one-line wiring change in main.py.
"""
from __future__ import annotations

from typing import Protocol

from ..schemas.chat import TextChatData, UserProfile


class AgentDataClient(Protocol):
    async def fetch_history(
        self, self_uid: int, peer_uid: int, limit: int, before_msg_db_id: int = 0
    ) -> list[TextChatData]: ...

    async def fetch_profile(self, peer_uid: int) -> UserProfile | None: ...


class GrpcAgentDataClient:
    """TODO(milestone: network integration).

    Responsibilities:
      - Establish grpc.aio channel to ChatServer (host/port from config).
      - Carry self_uid + server token in metadata for auth.
      - Map proto messages <-> TextChatData / UserProfile.
      - Connection pooling / retry-with-backoff on transient failure.
    """

    def __init__(self, target: str) -> None:
        self._target = target
        raise NotImplementedError(
            "GrpcAgentDataClient is a milestone-2 placeholder. "
            "Use MockBackend (Backend.Mode=mock) until AgentDataService proto + "
            "server-side impl land."
        )

    async def fetch_history(self, *args, **kwargs):  # pragma: no cover
        raise NotImplementedError

    async def fetch_profile(self, *args, **kwargs):  # pragma: no cover
        raise NotImplementedError
