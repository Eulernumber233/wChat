"""Chat data contracts.

Field names align with:
  - wChat_client/userdata.h TextChatData (client-side)
  - wChat_server/wChat_server_tcp/MysqlDao chat_messages table
  - Redis key `ubaseinfo_<uid>` payload

msg_db_id uses str on the wire because jsoncpp (used by C++ services) lacks
int64 support; see CLAUDE.md §1. Internal arithmetic converts to int.
"""
from __future__ import annotations

from enum import IntEnum

from pydantic import BaseModel, ConfigDict, Field, field_validator


class MsgType(IntEnum):
    TEXT = 1
    IMAGE = 2
    FILE = 3
    AUDIO = 4


class Direction(IntEnum):
    INCOMING = 0  # peer -> self
    OUTGOING = 1  # self -> peer


class TextChatData(BaseModel):
    """One message row. Mirrors LocalDb.MsgRow + TextChatData."""

    model_config = ConfigDict(use_enum_values=False)

    msg_db_id: str = Field(description="Server-side chat_messages.id (int64 as string)")
    from_uid: int
    to_uid: int
    msg_type: MsgType
    content: str = Field(description="Text content, or JSON metadata for non-text messages")
    send_time: int = Field(description="Unix seconds")
    direction: Direction

    @field_validator("msg_db_id", mode="before")
    @classmethod
    def _coerce_id(cls, v: object) -> str:
        return str(v)


class UserProfile(BaseModel):
    """Peer's base info, corresponds to Redis `ubaseinfo_<uid>`."""

    uid: int
    name: str
    nick: str | None = None
    sex: int | None = None
    desc: str | None = None
    icon: str | None = None
