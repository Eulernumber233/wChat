"""POST /agent/suggest_reply/stream — SSE streaming variant.

Event sequence:
  1. "intent"          — analysis result + tools used (one event)
  2. "candidate_delta" — raw LLM token fragments (many events)
  3. "candidate_done"  — parsed candidate object (one per candidate)
  4. "done"            — session_id + strategy + token count
  On failure at any phase: "error" event, then stream ends.

The client should buffer candidate_delta text and display it as a
typing indicator; candidate_done events carry the final parsed objects.
"""
from __future__ import annotations

import logging

from fastapi import APIRouter, Depends, HTTPException
from starlette.responses import StreamingResponse

from ..agent.graph import AgentService
from ..presets.loader import get_preset_store
from ..schemas.request import SuggestReplyRequest
from ..security.auth import AuthContext, enforce_self_uid, require_auth
from ..security.rate_limit import check_and_incr
from .deps import get_agent_service

log = logging.getLogger(__name__)
router = APIRouter(prefix="/agent", tags=["agent"])


@router.post("/suggest_reply/stream")
async def suggest_reply_stream(
    req: SuggestReplyRequest,
    agent: AgentService = Depends(get_agent_service),
    ctx: AuthContext = Depends(require_auth),
) -> StreamingResponse:
    enforce_self_uid(ctx, req.self_uid)

    if not req.recent_messages:
        raise HTTPException(status_code=400, detail="recent_messages must not be empty")

    preset = get_preset_store().get(req.preset_id)
    if req.preset_id and preset is None:
        raise HTTPException(status_code=404, detail=f"preset {req.preset_id} not found")

    await check_and_incr(ctx.self_uid)

    log.info(
        "POST /agent/suggest_reply/stream self=%s peer=%s preset=%s n=%d",
        req.self_uid, req.peer_uid, req.preset_id, req.num_candidates,
    )

    return StreamingResponse(
        agent.suggest_reply_stream(req, preset, auth_token=ctx.token),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no",
        },
    )
