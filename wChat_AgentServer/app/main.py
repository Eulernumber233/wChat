"""FastAPI entrypoint. Run with:

    uvicorn app.main:app --reload --port 8200
"""
from __future__ import annotations

import logging
from contextlib import asynccontextmanager
from typing import AsyncIterator

from fastapi import FastAPI

from .api import health, presets, suggest, suggest_stream
from .config.settings import get_settings

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
)


@asynccontextmanager
async def _lifespan(_app: FastAPI) -> AsyncIterator[None]:
    s = get_settings()
    logging.getLogger(__name__).info(
        "AgentServer starting on %s:%s backend=%s llm=%s/%s",
        s.server.host,
        s.server.port,
        s.backend.mode,
        s.llm.provider,
        s.llm.model,
    )
    yield


def create_app() -> FastAPI:
    app = FastAPI(
        title="wChat AgentServer",
        version="0.1.0",
        description="Smart reply suggestion for the wChat IM client.",
        lifespan=_lifespan,
    )
    app.include_router(health.router)
    app.include_router(presets.router)
    app.include_router(suggest.router)
    app.include_router(suggest_stream.router)
    return app


app = create_app()
