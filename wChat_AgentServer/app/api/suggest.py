"""POST /agent/suggest_reply — blocking, returns all candidates at once."""
from __future__ import annotations

import logging

from fastapi import APIRouter, Depends, HTTPException

from ..agent.graph import AgentService
from ..presets.loader import get_preset_store
from ..schemas.request import RefineRequest, SuggestReplyRequest
from ..schemas.response import Candidate, SuggestReplyResponse
from ..security.auth import AuthContext, enforce_self_uid, require_auth
from ..security.rate_limit import check_and_incr
from .deps import get_agent_service

log = logging.getLogger(__name__)
router = APIRouter(prefix="/agent", tags=["agent"])


@router.post("/suggest_reply", response_model=SuggestReplyResponse)
async def suggest_reply(
    req: SuggestReplyRequest,
    agent: AgentService = Depends(get_agent_service),
    ctx: AuthContext = Depends(require_auth),
) -> SuggestReplyResponse:
    enforce_self_uid(ctx, req.self_uid)
    log.info(
        "POST /agent/suggest_reply self=%s peer=%s preset=%s n=%d has_custom_prompt=%s",
        req.self_uid, req.peer_uid, req.preset_id, req.num_candidates,
        bool(req.custom_prompt),
    )
    if not req.recent_messages:
        raise HTTPException(status_code=400, detail="recent_messages must not be empty")
    preset = get_preset_store().get(req.preset_id)
    if req.preset_id and preset is None:
        raise HTTPException(status_code=404, detail=f"preset {req.preset_id} not found")

    await check_and_incr(ctx.self_uid)

    try:
        return await agent.suggest_reply(req, preset, auth_token=ctx.token)
    except Exception as e:  # noqa: BLE001
        log.exception("suggest_reply failed")
        raise HTTPException(status_code=500, detail=f"agent_error: {e}") from e


@router.post("/refine", response_model=Candidate)
async def refine(
    req: RefineRequest,
    agent: AgentService = Depends(get_agent_service),
    ctx: AuthContext = Depends(require_auth),
) -> Candidate:
    log.info(
        "POST /agent/refine session_id=%s idx=%d uid=%d",
        req.session_id, req.candidate_index, ctx.self_uid,
    )
    # ownership check BEFORE burning LLM budget — cheap and safer
    rec = await agent.memory.get(req.session_id)
    if rec is None:
        raise HTTPException(status_code=404, detail=f"session {req.session_id} not found or expired")
    if rec.self_uid != ctx.self_uid:
        raise HTTPException(status_code=403, detail="session belongs to a different user")

    await check_and_incr(ctx.self_uid)
    try:
        return await agent.refine(req)
    except KeyError as e:
        raise HTTPException(status_code=404, detail=str(e)) from e
    except IndexError as e:
        raise HTTPException(status_code=400, detail=str(e)) from e
    except Exception as e:  # noqa: BLE001
        log.exception("refine failed")
        raise HTTPException(status_code=500, detail=f"agent_error: {e}") from e
