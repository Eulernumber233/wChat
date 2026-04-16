"""Tool abstraction + registry.

Each tool exposes an OpenAI-style JSON schema so the agent can present
them to the LLM for function-calling. Agent code should go through the
registry, not import tools directly, so swapping Mock->gRPC only edits
one wiring point.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Protocol


@dataclass
class ToolResult:
    """Uniform return type; `ok=False` means the tool failed but agent
    should keep going (don't crash the whole graph on a data fetch miss)."""

    ok: bool
    data: Any
    error: str | None = None


class Tool(Protocol):
    name: str
    description: str
    parameters: dict[str, Any]  # JSON schema for arguments

    async def run(self, **kwargs: Any) -> ToolResult: ...


class ToolRegistry:
    def __init__(self) -> None:
        self._tools: dict[str, Tool] = {}

    def register(self, tool: Tool) -> None:
        self._tools[tool.name] = tool

    def get(self, name: str) -> Tool | None:
        return self._tools.get(name)

    def all(self) -> list[Tool]:
        return list(self._tools.values())

    def openai_schemas(self) -> list[dict[str, Any]]:
        """Return tools in OpenAI function-calling format."""
        return [
            {
                "type": "function",
                "function": {
                    "name": t.name,
                    "description": t.description,
                    "parameters": t.parameters,
                },
            }
            for t in self._tools.values()
        ]
