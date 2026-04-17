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
from typing import Any, AsyncIterator

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

def build_graph(llm: LLMProvider, tools: ToolRegistry) -> Any:
    """Full pipeline: intent → fetch_* → generate. Used by blocking suggest_reply."""
    g: StateGraph = StateGraph(AgentState)

    g.add_node("analyze_intent", functools.partial(nodes.analyze_intent_node, llm=llm))
    g.add_node("fetch_profile", functools.partial(nodes.fetch_profile_node, tools=tools))
    g.add_node("fetch_history", functools.partial(nodes.fetch_history_node, tools=tools))
    g.add_node("fetch_summary", functools.partial(nodes.fetch_summary_node, tools=tools))
    g.add_node("generate", functools.partial(nodes.generate_candidates_node, llm=llm))

    g.add_edge(START, "analyze_intent")
    g.add_edge("analyze_intent", "fetch_profile")
    g.add_edge("fetch_profile", "fetch_history")
    g.add_edge("fetch_history", "fetch_summary")
    g.add_edge("fetch_summary", "generate")
    g.add_edge("generate", END)

    return g.compile()


def build_prep_graph(llm: LLMProvider, tools: ToolRegistry) -> Any:
    """Prep-only pipeline: intent → fetch_* (no generate).

    Used by the streaming endpoint to run the cheap/fast part blocking,
    then hand off generate to a token-by-token LLM stream.
    """
    g: StateGraph = StateGraph(AgentState)

    g.add_node("analyze_intent", functools.partial(nodes.analyze_intent_node, llm=llm))
    g.add_node("fetch_profile", functools.partial(nodes.fetch_profile_node, tools=tools))
    g.add_node("fetch_history", functools.partial(nodes.fetch_history_node, tools=tools))
    g.add_node("fetch_summary", functools.partial(nodes.fetch_summary_node, tools=tools))

    g.add_edge(START, "analyze_intent")
    g.add_edge("analyze_intent", "fetch_profile")
    g.add_edge("fetch_profile", "fetch_history")
    g.add_edge("fetch_history", "fetch_summary")
    g.add_edge("fetch_summary", END)

    return g.compile()


class AgentService:
    """Top-level facade the API layer calls.

    Tool registry carries a per-request auth_token (forwarded to the gRPC
    backend so ChatServer can validate against Redis utoken_<uid>). That
    means the registry — and the graph that binds it via functools.partial
    — must be built fresh per request. Graph compilation is microseconds
    on our 5-node topology, so skipping the old per-(self,peer) cache
    costs nothing.
    """

    def __init__(
        self,
        llm: LLMProvider,
        tool_factory,  # Callable[[int, int, str], ToolRegistry]
        memory: MemoryStore,
    ) -> None:
        self._llm = llm
        self._tool_factory = tool_factory
        self._memory = memory

    # 为外部代码提供只读的 MemoryStore 访问接口
    @property
    def memory(self) -> MemoryStore:
        return self._memory

    def _build_graph(self, self_uid: int, peer_uid: int, auth_token: str) -> Any:
        tools = self._tool_factory(self_uid, peer_uid, auth_token)
        return build_graph(self._llm, tools)

    async def suggest_reply(
        self,
        req: SuggestReplyRequest,
        preset: Preset | None,
        *,
        auth_token: str = "",
    ) -> SuggestReplyResponse:
        t0 = time.time()
        log.info(
            "suggest_reply start self_uid=%s peer_uid=%s preset=%s n=%d msgs=%d",
            req.self_uid, req.peer_uid,
            preset.id if preset else None,
            req.num_candidates, len(req.recent_messages),
        )
        graph = self._build_graph(req.self_uid, req.peer_uid, auth_token)

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

    async def suggest_reply_stream(
        self,
        req: SuggestReplyRequest,
        preset: Preset | None,
        *,
        auth_token: str = "",
    ) -> AsyncIterator[str]:
        """SSE-oriented generator.

        Yields SSE-formatted strings. Flow:
          1. Run prep graph (intent + tools, blocking) → emit "intent" event
          2. Stream generate LLM call token by token → emit "candidate_delta" events
          3. Parse the accumulated JSON → emit "candidate_done" per candidate
          4. Emit "done" with session_id

        If any step fails, emits an "error" event and stops.
        """
        import json as _json

        session_id = req.session_id or uuid.uuid4().hex

        def _sse(event: str, data: dict) -> str:
            return f"event: {event}\ndata: {_json.dumps(data, ensure_ascii=False)}\n\n"

        # ---- Phase 1: prep (intent + tool fetch) ----
        tools = self._tool_factory(req.self_uid, req.peer_uid, auth_token)
        prep_graph = build_prep_graph(self._llm, tools)

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

        try:
            prep_state: AgentState = await prep_graph.ainvoke(initial)
        except Exception as exc:
            log.exception("stream prep failed")
            yield _sse("error", {"detail": f"prep_failed: {exc}"})
            return

        yield _sse("intent", {
            "intent": prep_state.get("intent") or "",
            "tools_used": prep_state.get("tools_used") or [],
        })

        # ---- Phase 2: stream generate ----
        user_prompt = prompts.build_generate_user_prompt(
            self_uid=req.self_uid,
            recent_messages=req.recent_messages,
            extended_history=prep_state.get("extended_history"),
            friend_profile_block=nodes._profile_block(prep_state),
            relationship_summary=prep_state.get("relationship_summary") or "(未获取)",
            preset_block=(preset.to_prompt_block() if preset else "(无固定场景,按自定义要求处理)"),
            custom_prompt=req.custom_prompt or "",
            intent=prep_state.get("intent") or "",
            num_candidates=req.num_candidates,
        )

        messages = [
            {"role": "system", "content": prompts.GENERATE_SYSTEM_PROMPT},
            {"role": "user", "content": user_prompt},
        ]

        accumulated = ""
        try:
            async for chunk in self._llm.stream(
                messages,
                response_format={"type": "json_object"},
                temperature=0.85,
                max_tokens=1200,
            ):
                if chunk.delta:
                    accumulated += chunk.delta
                    yield _sse("candidate_delta", {"delta": chunk.delta})
        except Exception as exc:
            log.exception("stream generate LLM failed")
            yield _sse("error", {"detail": f"llm_stream_failed: {exc}"})
            return

        # ---- Phase 3: parse accumulated JSON, emit candidates ----
        parsed = nodes._safe_json_loads(accumulated)
        strategy = parsed.get("strategy", "")
        raw_cands = parsed.get("candidates") or []
        candidates: list[Candidate] = []
        for i, c in enumerate(raw_cands):
            try:
                cand = Candidate(
                    index=int(c.get("index", i)),
                    style=str(c.get("style", ""))[:32],
                    content=str(c.get("content", "")).strip(),
                    reasoning=str(c.get("reasoning", "")) or None,
                )
                candidates.append(cand)
                yield _sse("candidate_done", cand.model_dump())
            except Exception as e:
                log.warning("stream candidate parse failed: %s; item=%r", e, c)

        # pad if needed
        while len(candidates) < req.num_candidates:
            pad = Candidate(
                index=len(candidates), style="占位",
                content="(生成失败,请重试)", reasoning="parse_error",
            )
            candidates.append(pad)
            yield _sse("candidate_done", pad.model_dump())

        # ---- Phase 4: persist + done ----
        final_state: AgentState = {
            **prep_state,
            "strategy": strategy,
            "candidates": candidates,
        }
        await self._memory.put(
            SessionRecord(
                session_id=session_id,
                self_uid=req.self_uid,
                peer_uid=req.peer_uid,
                state_snapshot=_dump_state(final_state),
                created_at=time.time(),
            )
        )

        yield _sse("done", {
            "session_id": session_id,
            "strategy": strategy,
            "tokens_used": int(prep_state.get("tokens_used") or 0),
        })

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
