"""LangGraph node implementations.

Each node is `async def (state) -> partial_state`. LangGraph merges the
partial dict into the running state. Keep nodes small and single-purpose
so the graph structure (graph.py) reads like a flowchart.
"""
from __future__ import annotations

import json
import logging
from typing import Any

from ..llm.base import LLMProvider
from ..schemas.chat import TextChatData
from ..schemas.response import Candidate
from ..tools.base import ToolRegistry
from . import prompts
from .state import AgentState

log = logging.getLogger(__name__)


def _append_tool_used(state: AgentState, name: str) -> list[str]:
    used = list(state.get("tools_used") or [])
    used.append(name)
    return used


def _append_error(state: AgentState, err: str) -> list[str]:
    errs = list(state.get("errors") or [])
    errs.append(err)
    return errs


def _bump_tokens(state: AgentState, delta: int) -> int:
    return int(state.get("tokens_used") or 0) + delta


def _safe_json_loads(raw: str) -> dict[str, Any]:
    """LLM sometimes wraps JSON in markdown fences even under JSON mode."""
    raw = raw.strip()
    if raw.startswith("```"):
        raw = raw.strip("`")
        # strip leading 'json\n'
        if raw.lower().startswith("json"):
            raw = raw[4:].lstrip()
    try:
        return json.loads(raw)
    except json.JSONDecodeError as e:
        log.warning("JSON parse failed: %s; raw=%r", e, raw[:200])
        return {}


# ------------------------------------------------------------------ #
# 1. analyze_intent                                                  #
# ------------------------------------------------------------------ #
async def analyze_intent_node(state: AgentState, *, llm: LLMProvider) -> AgentState:
    log.info(
        "[analyze_intent] start self_uid=%s peer_uid=%s n_msgs=%d",
        state["self_uid"], state["peer_uid"], len(state["recent_messages"]),
    )
    msgs_block = prompts.format_messages(state["recent_messages"], state["self_uid"])
    user_prompt = f"最近聊天(旧→新):\n{msgs_block}\n\n请输出意图分析 JSON。"

    resp = await llm.complete(
        messages=[
            {"role": "system", "content": prompts.INTENT_SYSTEM_PROMPT},
            {"role": "user", "content": user_prompt},
        ],
        response_format={"type": "json_object"},
        temperature=0.2,
        max_tokens=300,
    )
    parsed = _safe_json_loads(resp.content)

    intent = parsed.get("intent", "(未能解析)")
    needs = parsed.get("needs_tools", []) or []
    if not isinstance(needs, list):
        needs = []
    log.info(
        "[analyze_intent] done tokens=%d intent=%r needs_tools=%s",
        resp.total_tokens, intent, needs,
    )

    # stash needs_tools on state via a side channel — reuse tools_used slot
    # for the planned set; tool nodes will filter themselves by this.
    return {
        "intent": intent,
        "tools_used": list(needs),  # used as "planned tools" here; cleared later
        "tokens_used": _bump_tokens(state, resp.total_tokens),
    }  # type: ignore[return-value]


# ------------------------------------------------------------------ #
# 2. conditional routing helpers                                     #
# ------------------------------------------------------------------ #
def needs_profile(state: AgentState) -> bool:
    return "get_friend_profile" in (state.get("tools_used") or [])


def needs_history(state: AgentState) -> bool:
    return "get_chat_history" in (state.get("tools_used") or [])


def needs_summary(state: AgentState) -> bool:
    return "get_relationship_summary" in (state.get("tools_used") or [])


# ------------------------------------------------------------------ #
# 3. tool nodes                                                      #
# ------------------------------------------------------------------ #
async def fetch_profile_node(state: AgentState, *, tools: ToolRegistry) -> AgentState:
    tool = tools.get("get_friend_profile")
    if not tool:
        return {}  # type: ignore[return-value]
    log.info("[fetch_profile] calling backend peer_uid=%s", state["peer_uid"])
    result = await tool.run()
    profile = None
    if result.ok and result.data:
        from ..schemas.chat import UserProfile

        profile = UserProfile(**result.data)
        log.info("[fetch_profile] ok name=%r nick=%r", profile.name, profile.nick)
    else:
        log.warning("[fetch_profile] failed error=%s", result.error)
        return {"errors": _append_error(state, f"get_friend_profile:{result.error}")}  # type: ignore[return-value]
    return {"friend_profile": profile}  # type: ignore[return-value]


async def fetch_history_node(state: AgentState, *, tools: ToolRegistry) -> AgentState:
    tool = tools.get("get_chat_history")
    if not tool:
        return {}  # type: ignore[return-value]
    # heuristic: fetch 30 more, excluding the already-seen window
    limit = 30
    log.info("[fetch_history] calling backend peer_uid=%s limit=%d", state["peer_uid"], limit)
    result = await tool.run(limit=limit, before_msg_db_id=0)
    if not result.ok:
        log.warning("[fetch_history] failed error=%s", result.error)
        return {"errors": _append_error(state, f"get_chat_history:{result.error}")}  # type: ignore[return-value]
    existing_ids = {m.msg_db_id for m in state["recent_messages"]}
    extended = [
        TextChatData(**m) for m in result.data if str(m.get("msg_db_id")) not in existing_ids
    ]
    log.info(
        "[fetch_history] ok fetched=%d new=%d", len(result.data), len(extended),
    )
    return {"extended_history": extended}  # type: ignore[return-value]


async def fetch_summary_node(state: AgentState, *, tools: ToolRegistry) -> AgentState:
    tool = tools.get("get_relationship_summary")
    if not tool:
        return {}  # type: ignore[return-value]
    log.info("[fetch_summary] calling backend peer_uid=%s", state["peer_uid"])
    result = await tool.run()
    if not result.ok:
        log.warning("[fetch_summary] failed error=%s", result.error)
        return {"errors": _append_error(state, f"get_relationship_summary:{result.error}")}  # type: ignore[return-value]
    log.info("[fetch_summary] ok summary_len=%d", len(str(result.data)))
    return {"relationship_summary": str(result.data)}  # type: ignore[return-value]


# ------------------------------------------------------------------ #
# 4. generate_candidates                                             #
# ------------------------------------------------------------------ #
def _profile_block(state: AgentState) -> str:
    p = state.get("friend_profile")
    if not p:
        return "(未获取)"
    return (
        f"uid={p.uid} name={p.name} "
        f"nick={p.nick or '-'} desc={p.desc or '-'}"
    )


async def generate_candidates_node(state: AgentState, *, llm: LLMProvider) -> AgentState:
    preset = state.get("preset")
    preset_block = preset.to_prompt_block() if preset else "(无固定场景,按自定义要求处理)"
    log.info(
        "[generate] start num_candidates=%d preset=%s has_ext_history=%s has_profile=%s has_summary=%s",
        state["num_candidates"],
        preset.id if preset else None,
        bool(state.get("extended_history")),
        bool(state.get("friend_profile")),
        bool(state.get("relationship_summary")),
    )

    user_prompt = prompts.build_generate_user_prompt(
        self_uid=state["self_uid"],
        recent_messages=state["recent_messages"],
        extended_history=state.get("extended_history"),
        friend_profile_block=_profile_block(state),
        relationship_summary=state.get("relationship_summary") or "(未获取)",
        preset_block=preset_block,
        custom_prompt=state.get("custom_prompt") or "",
        intent=state.get("intent") or "",
        num_candidates=state["num_candidates"],
    )

    resp = await llm.complete(
        messages=[
            {"role": "system", "content": prompts.GENERATE_SYSTEM_PROMPT},
            {"role": "user", "content": user_prompt},
        ],
        response_format={"type": "json_object"},
        temperature=0.85,
        max_tokens=1200,
    )
    log.info("[generate] llm returned tokens=%d content_len=%d",
             resp.total_tokens, len(resp.content))
    parsed = _safe_json_loads(resp.content)

    strategy = parsed.get("strategy", "(未能解析)")
    raw_cands = parsed.get("candidates") or []
    candidates: list[Candidate] = []
    for i, c in enumerate(raw_cands):
        try:
            candidates.append(
                Candidate(
                    index=int(c.get("index", i)),
                    style=str(c.get("style", ""))[:32],
                    content=str(c.get("content", "")).strip(),
                    reasoning=str(c.get("reasoning", "")) or None,
                )
            )
        except Exception as e:  # noqa: BLE001
            log.warning("candidate parse failed: %s; item=%r", e, c)

    # enforce exact count: pad or truncate
    n = state["num_candidates"]
    if len(candidates) > n:
        candidates = candidates[:n]
    while len(candidates) < n:
        candidates.append(
            Candidate(
                index=len(candidates),
                style="占位",
                content="(生成失败,请重试)",
                reasoning="parse_error",
            )
        )
    for i, c in enumerate(candidates):
        c.index = i  # normalize

    log.info(
        "[generate] done strategy=%r n_candidates=%d styles=%s",
        strategy, len(candidates), [c.style for c in candidates],
    )

    return {
        "strategy": strategy,
        "candidates": candidates,
        "tokens_used": _bump_tokens(state, resp.total_tokens),
        # clear the planner's hint so downstream uses it as "actually used"
        "tools_used": [
            t for t in ("get_friend_profile", "get_chat_history", "get_relationship_summary")
            if t in (state.get("tools_used") or [])
        ],
    }  # type: ignore[return-value]
