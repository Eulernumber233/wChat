"""Preset loader. Parsed once at startup, no hot-reload (matches
ConfigMgr convention on the C++ side)."""
from __future__ import annotations

from pathlib import Path

import yaml

from ..config.settings import get_settings
from .models import Preset


class PresetStore:
    def __init__(self, presets: list[Preset]) -> None:
        self._by_id = {p.id: p for p in presets}

    def get(self, preset_id: str | None) -> Preset | None:
        if not preset_id:
            return None
        return self._by_id.get(preset_id)

    def all(self) -> list[Preset]:
        return list(self._by_id.values())


def load_presets(path: str | Path) -> PresetStore:
    path = Path(path)
    if not path.exists():
        return PresetStore([])
    raw = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    items = raw.get("presets", [])
    return PresetStore([Preset(**item) for item in items])


_cached: PresetStore | None = None


def get_preset_store() -> PresetStore:
    global _cached
    if _cached is None:
        _cached = load_presets(get_settings().presets_path)
    return _cached
