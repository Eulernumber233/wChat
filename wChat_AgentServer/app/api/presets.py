"""GET /agent/presets — list configured scenario presets."""
from __future__ import annotations

from fastapi import APIRouter

from ..presets.loader import get_preset_store
from ..presets.models import Preset

router = APIRouter(prefix="/agent", tags=["agent"])


@router.get("/presets", response_model=list[Preset])
async def list_presets() -> list[Preset]:
    return get_preset_store().all()
