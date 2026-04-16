"""LangGraph assembly + top-level AgentService.

Graph shape:
    START
      │
      ▼
    analyze_intent          ← one LLM call, returns intent + which tools to use
      │
      ▼
    (fan-out to whichever tools the planner asked for; nodes no-op otherwise)
    fetch_profile ─┐
    fetch_history ─┼─▶ generate_candidates ─▶ END
    fetch_summary ─┘

We wire tool nodes as always-present edges and let each tool node
early-return if the planner didn't ask for it. This keeps the graph
topology static (easier to reason about) at the cost of 3 cheap
no-op calls when tools aren't needed.
"""
from __future__ import annotations

import functools
import logging
import time
import uuid
from typing import Any

from langgraph.graph import END, START, StateGraph

from ..llm.base import LLMProvider
from ..memory.base import MemoryStore, SessionRecord
from ..presets.models import Preset
from ..schemas.request import RefineRequest, SuggestReplyRequest
from ..schemas.response import Candidate, SuggestReplyResponse
from ..tools.base import ToolRegistry
from . import nodes, prompts
from .state import AgentState

log = logging.getLogger(__name__)

# 线性流水线
def build_graph(llm: LLMProvider, tools: ToolRegistry) -> Any:
    """Build and compile the LangGraph. Cheap — call once at startup."""
    g: StateGraph = StateGraph(AgentState)

    g.add_node("analyze_intent", functools.partial(nodes.analyze_intent_node, llm=llm))
    g.add_node("fetch_profile", functools.partial(nodes.fetch_profile_node, tools=tools))
    g.add_node("fetch_history", functools.partial(nodes.fetch_history_node, tools=tools))
    g.add_node("fetch_summary", functools.partial(nodes.fetch_summary_node, tools=tools))
    g.add_node("generate", functools.partial(nodes.generate_candidates_node, llm=llm))

    g.add_edge(START, "analyze_intent")
    # simple linear: profile -> history -> summary -> generate.
    # each tool node checks its own "needed" flag and returns {} if not.
    g.add_edge("analyze_intent", "fetch_profile")
    g.add_edge("fetch_profile", "fetch_history")
    g.add_edge("fetch_history", "fetch_summary")
    g.add_edge("fetch_summary", "generate")
    g.add_edge("generate", END)

    return g.compile()


class AgentService:
    """Top-level facade the API layer calls.

    Holds the compiled graph + toolregistry factory. Per-request we build
    a fresh registry because tools are parameterized by (self_uid, peer_uid).
    """

    def __init__(
        self,
        llm: LLMProvider,
        tool_factory,  # Callable[[int, int], ToolRegistry]
        memory: MemoryStore,
    ) -> None:
        self._llm = llm
        self._tool_factory = tool_factory
        self._memory = memory
        self._graph_cache: dict[tuple[int, int], Any] = {}

    def _graph_for(self, self_uid: int, peer_uid: int) -> Any:
        # compile once per peer pair — cheap to cache
        key = (self_uid, peer_uid)
        g = self._graph_cache.get(key)
        if g is None:
            tools = self._tool_factory(self_uid, peer_uid) # 工具构造（每对用户一次）
            g = build_graph(self._llm, tools)
            self._graph_cache[key] = g
        return g

    async def suggest_reply(
        self, req: SuggestReplyRequest, preset: Preset | None
    ) -> SuggestReplyResponse:
        t0 = time.time()
        log.info(
            "suggest_reply start self_uid=%s peer_uid=%s preset=%s n=%d msgs=%d",
            req.self_uid, req.peer_uid,
            preset.id if preset else None,
            req.num_candidates, len(req.recent_messages),
        )
        graph = self._graph_for(req.self_uid, req.peer_uid)

        initial: AgentState = {
            "self_uid": req.self_uid,
            "peer_uid": req.peer_uid,
            "recent_messages": req.recent_messages,
            "preset": preset,
            "custom_prompt": req.custom_prompt,
            "num_candidates": req.num_candidates,
            "tools_used": [],
            "errors": [],
            "tokens_used": 0,
        }

        final: AgentState = await graph.ainvoke(initial)  # type: ignore[assignment]

        # 持久化会话快照，供/refine使用
        session_id = req.session_id or uuid.uuid4().hex
        await self._memory.put(
            SessionRecord(
                session_id=session_id,
                self_uid=req.self_uid,
                peer_uid=req.peer_uid,
                state_snapshot=_dump_state(final),
                created_at=time.time(),
            )
        )

        elapsed_ms = int((time.time() - t0) * 1000)
        log.info(
            "suggest_reply done session_id=%s elapsed_ms=%d tokens=%d tools_used=%s errors=%s",
            session_id, elapsed_ms,
            int(final.get("tokens_used") or 0),
            final.get("tools_used") or [],
            final.get("errors") or [],
        )

        return SuggestReplyResponse(
            session_id=session_id,
            candidates=final.get("candidates") or [],
            intent_analysis=final.get("intent") or "",
            strategy=final.get("strategy") or "",
            tokens_used=int(final.get("tokens_used") or 0),
            tools_used=final.get("tools_used") or [],
        )

    async def refine(self, req: RefineRequest) -> Candidate:
        log.info(
            "refine start session_id=%s candidate_index=%d instruction=%r",
            req.session_id, req.candidate_index, req.instruction,
        )
        rec = await self._memory.get(req.session_id)
        if rec is None:
            log.warning("refine session missing session_id=%s", req.session_id)
            raise KeyError(f"session {req.session_id} not found or expired")
        result = await refine_candidate(self._llm, rec, req)
        log.info("refine done session_id=%s style=%r", req.session_id, result.style)
        return result


# 将 AgentState 递归转换为普通字典（Pydantic 模型调用 model_dump()，普通值原样保留）
def _dump_state(state: AgentState) -> dict[str, Any]:
    """Convert AgentState into a JSON-ish snapshot for memory storage.

    Pydantic models need model_dump(); leave plain types alone.
    """
    out: dict[str, Any] = {}
    for k, v in state.items():
        if hasattr(v, "model_dump"):
            out[k] = v.model_dump()
        elif isinstance(v, list):
            out[k] = [x.model_dump() if hasattr(x, "model_dump") else x for x in v]
        else:
            out[k] = v
    return out


async def refine_candidate(
    llm: LLMProvider, record: SessionRecord, req: RefineRequest
) -> Candidate:
    """Regenerate one candidate against a prior session + a user instruction.

    Skips the graph — this is a single focused LLM call.
    """
    snap = record.state_snapshot
    cands = snap.get("candidates") or []
    if req.candidate_index >= len(cands):
        raise IndexError(f"candidate_index {req.candidate_index} out of range")
    original = cands[req.candidate_index]

    user_prompt = f"""【原候选】
style: {original.get('style')}
content: {original.get('content')}

【修改指令】
{req.instruction}

请给出改进后的单条候选 JSON。"""

    resp = await llm.complete(
        messages=[
            {"role": "system", "content": prompts.REFINE_SYSTEM_PROMPT},
            {"role": "user", "content": user_prompt},
        ],
        response_format={"type": "json_object"},
        temperature=0.8,
        max_tokens=400,
    )
    parsed = nodes._safe_json_loads(resp.content)
    return Candidate(
        index=req.candidate_index,
        style=str(parsed.get("style", original.get("style", "")))[:32],
        content=str(parsed.get("content", "")).strip() or original.get("content", ""),
        reasoning=str(parsed.get("reasoning", "")) or None,
    )
