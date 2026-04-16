"""DeepSeek provider.

DeepSeek is OpenAI-compatible, so we reuse the openai SDK with a custom
base_url. Model choices:
  - deepseek-chat: fast general-purpose, recommended default
  - deepseek-reasoner: slower + chain-of-thought, use when intent analysis
    quality matters more than latency
"""
from __future__ import annotations

import logging
import time
from typing import Any, AsyncIterator

from openai import AsyncOpenAI

from .base import LLMChunk, LLMResponse, ToolCall

log = logging.getLogger(__name__)


class DeepSeekProvider:
    def __init__(
        self,
        api_key: str,
        base_url: str = "https://api.deepseek.com",
        model: str = "deepseek-chat",
    ) -> None:
        if not api_key:
            raise ValueError("DeepSeek api_key is required (set DEEPSEEK_API_KEY env var)")
        self._client = AsyncOpenAI(api_key=api_key, base_url=base_url)
        self._model = model

    async def complete(
        self,
        messages: list[dict[str, Any]],
        *,
        tools: list[dict[str, Any]] | None = None,
        response_format: dict[str, Any] | None = None,
        temperature: float = 0.7,
        max_tokens: int | None = None,
    ) -> LLMResponse:
        kwargs: dict[str, Any] = {
            "model": self._model,
            "messages": messages,
            "temperature": temperature,
        }
        if tools:
            kwargs["tools"] = tools
        if response_format:
            kwargs["response_format"] = response_format
        if max_tokens:
            kwargs["max_tokens"] = max_tokens

        log.info(
            "deepseek.complete model=%s temp=%.2f n_messages=%d json_mode=%s",
            self._model, temperature, len(messages), bool(response_format),
        )
        t0 = time.time()
        resp = await self._client.chat.completions.create(**kwargs)
        elapsed_ms = int((time.time() - t0) * 1000)
        msg = resp.choices[0].message

        tool_calls: list[ToolCall] = []
        if msg.tool_calls:
            import json

            for tc in msg.tool_calls:
                try:
                    args = json.loads(tc.function.arguments or "{}")
                except json.JSONDecodeError:
                    args = {}
                tool_calls.append(ToolCall(id=tc.id, name=tc.function.name, arguments=args))

        usage = resp.usage
        prompt_toks = usage.prompt_tokens if usage else 0
        completion_toks = usage.completion_tokens if usage else 0
        log.info(
            "deepseek.complete done elapsed_ms=%d prompt_tokens=%d completion_tokens=%d",
            elapsed_ms, prompt_toks, completion_toks,
        )
        return LLMResponse(
            content=msg.content or "",
            tool_calls=tool_calls,
            prompt_tokens=prompt_toks,
            completion_tokens=completion_toks,
        )

    async def stream(
        self,
        messages: list[dict[str, Any]],
        *,
        tools: list[dict[str, Any]] | None = None,
        response_format: dict[str, Any] | None = None,
        temperature: float = 0.7,
        max_tokens: int | None = None,
    ) -> AsyncIterator[LLMChunk]:
        kwargs: dict[str, Any] = {
            "model": self._model,
            "messages": messages,
            "temperature": temperature,
            "stream": True,
        }
        if tools:
            kwargs["tools"] = tools
        if response_format:
            kwargs["response_format"] = response_format
        if max_tokens:
            kwargs["max_tokens"] = max_tokens

        stream = await self._client.chat.completions.create(**kwargs)
        async for chunk in stream:
            if not chunk.choices:
                continue
            choice = chunk.choices[0]
            delta_obj = choice.delta
            yield LLMChunk(
                delta=delta_obj.content or "",
                tool_call_delta=(
                    {"tool_calls": [tc.model_dump() for tc in delta_obj.tool_calls]}
                    if delta_obj.tool_calls
                    else None
                ),
                finish_reason=choice.finish_reason,
            )
