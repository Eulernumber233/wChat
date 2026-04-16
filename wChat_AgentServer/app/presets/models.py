from __future__ import annotations

from pydantic import BaseModel, Field


class Preset(BaseModel):
    id: str
    name: str
    description: str
    style_hints: list[str] = Field(default_factory=list)

    def to_prompt_block(self) -> str:
        hints = "\n".join(f"  - {h}" for h in self.style_hints) or "  - (无)"
        return f"场景: {self.name}\n描述: {self.description}\n风格要点:\n{hints}"
