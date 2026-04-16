from __future__ import annotations

from fastapi import APIRouter

router = APIRouter(tags=["meta"])


@router.get("/agent/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}
