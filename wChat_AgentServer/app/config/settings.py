"""Configuration loader.

Priority: environment variables > agent.ini > built-in defaults.
`.env` is auto-loaded by pydantic-settings. INI parsing mimics the C++
ConfigMgr style (simple `[Section] key=value`) so ops can reuse habits.
"""
from __future__ import annotations

import configparser
import os
from dataclasses import dataclass, field
from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict


class EnvSettings(BaseSettings):
    """Secrets & deployment knobs pulled from environment / .env."""

    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    deepseek_api_key: str = ""
    deepseek_base_url: str = "https://api.deepseek.com"
    deepseek_model: str = "deepseek-chat"
    agent_config_path: str = "config/agent.ini"


@dataclass
class ServerConfig:
    host: str = "0.0.0.0"
    port: int = 8200


@dataclass
class LLMConfig:
    provider: str = "deepseek"
    model: str = "deepseek-chat"
    base_url: str = "https://api.deepseek.com"
    api_key: str = ""


@dataclass
class AgentConfig:
    max_history_messages: int = 50
    default_num_candidates: int = 3
    enable_relationship_summary: bool = True
    enable_rag: bool = False
    temperature: float = 0.7


@dataclass
class BackendConfig:
    mode: str = "mock"  # 选择信息获取方式 "mock" | "grpc"
    fixture_path: str = "tests/fixtures/sample_chats.json"
    # gRPC target of ChatServer (AgentDataService). Multi-instance reserved:
    # comma-separated list -> simple round-robin at channel creation.
    grpc_targets: str = "127.0.0.1:50055"


@dataclass
class RateLimitConfig:
    per_user_per_day: int = 100
    enabled: bool = True


@dataclass
class AuthConfig:
    # "off": accept any (or missing) Bearer — dev convenience
    # "redis": GET utoken_<uid> must equal the Bearer token
    mode: str = "redis"
    # Redis key template; {uid} is substituted
    token_key_template: str = "utoken_{uid}"


@dataclass
class MemoryConfig:
    backend: str = "memory"  # "memory" | "redis"
    session_ttl_seconds: int = 3600
    session_key_template: str = "agent_session:{sid}"


@dataclass
class RedisConfig:
    host: str = "127.0.0.1"
    port: int = 6380
    password: str = "123456"
    db: int = 0


@dataclass
class Settings:
    server: ServerConfig = field(default_factory=ServerConfig)
    llm: LLMConfig = field(default_factory=LLMConfig)
    agent: AgentConfig = field(default_factory=AgentConfig)
    backend: BackendConfig = field(default_factory=BackendConfig)
    rate_limit: RateLimitConfig = field(default_factory=RateLimitConfig)
    auth: AuthConfig = field(default_factory=AuthConfig)
    memory: MemoryConfig = field(default_factory=MemoryConfig)
    redis: RedisConfig = field(default_factory=RedisConfig)
    presets_path: str = "config/presets.yaml"


def _parse_ini(path: Path) -> configparser.ConfigParser:
    cp = configparser.ConfigParser()
    if path.exists():
        cp.read(path, encoding="utf-8")
    return cp


def _get_bool(cp: configparser.ConfigParser, section: str, key: str, default: bool) -> bool:
    if not cp.has_option(section, key):
        return default
    return cp.get(section, key).strip().lower() in {"1", "true", "yes", "on"}


def load_settings() -> Settings:
    env = EnvSettings()
    ini = _parse_ini(Path(env.agent_config_path))

    s = Settings()

    if ini.has_section("Server"):
        s.server.host = ini.get("Server", "Host", fallback=s.server.host)
        s.server.port = ini.getint("Server", "Port", fallback=s.server.port)

    if ini.has_section("LLM"):
        s.llm.provider = ini.get("LLM", "Provider", fallback=s.llm.provider)
        s.llm.model = ini.get("LLM", "Model", fallback=s.llm.model)
        s.llm.base_url = ini.get("LLM", "BaseUrl", fallback=s.llm.base_url)
        s.llm.api_key = ini.get("LLM", "ApiKey", fallback="")

    # env overrides ini for secrets + deployment overrides
    if env.deepseek_api_key:
        s.llm.api_key = env.deepseek_api_key
    if env.deepseek_base_url:
        s.llm.base_url = env.deepseek_base_url
    if env.deepseek_model:
        s.llm.model = env.deepseek_model

    if ini.has_section("Agent"):
        s.agent.max_history_messages = ini.getint(
            "Agent", "MaxHistoryMessages", fallback=s.agent.max_history_messages
        )
        s.agent.default_num_candidates = ini.getint(
            "Agent", "DefaultNumCandidates", fallback=s.agent.default_num_candidates
        )
        s.agent.enable_relationship_summary = _get_bool(
            ini, "Agent", "EnableRelationshipSummary", s.agent.enable_relationship_summary
        )
        s.agent.enable_rag = _get_bool(ini, "Agent", "EnableRAG", s.agent.enable_rag)
        s.agent.temperature = ini.getfloat("Agent", "Temperature", fallback=s.agent.temperature)

    if ini.has_section("Backend"):
        s.backend.mode = ini.get("Backend", "Mode", fallback=s.backend.mode)
        s.backend.fixture_path = ini.get("Backend", "FixturePath", fallback=s.backend.fixture_path)
        s.backend.grpc_targets = ini.get("Backend", "GrpcTargets", fallback=s.backend.grpc_targets)

    if ini.has_section("RateLimit"):
        s.rate_limit.per_user_per_day = ini.getint(
            "RateLimit", "PerUserPerDay", fallback=s.rate_limit.per_user_per_day
        )
        s.rate_limit.enabled = _get_bool(ini, "RateLimit", "Enabled", s.rate_limit.enabled)

    if ini.has_section("Auth"):
        s.auth.mode = ini.get("Auth", "Mode", fallback=s.auth.mode)
        s.auth.token_key_template = ini.get(
            "Auth", "TokenKeyTemplate", fallback=s.auth.token_key_template
        )

    if ini.has_section("Memory"):
        s.memory.backend = ini.get("Memory", "Backend", fallback=s.memory.backend)
        s.memory.session_ttl_seconds = ini.getint(
            "Memory", "SessionTtlSeconds", fallback=s.memory.session_ttl_seconds
        )
        s.memory.session_key_template = ini.get(
            "Memory", "SessionKeyTemplate", fallback=s.memory.session_key_template
        )

    if ini.has_section("Redis"):
        s.redis.host = ini.get("Redis", "Host", fallback=s.redis.host)
        s.redis.port = ini.getint("Redis", "Port", fallback=s.redis.port)
        s.redis.password = ini.get("Redis", "Password", fallback=s.redis.password)
        s.redis.db = ini.getint("Redis", "Db", fallback=s.redis.db)

    return s


_cached: Settings | None = None


def get_settings() -> Settings:
    """Module-level singleton. Tests can call reset_settings() to force reload."""
    global _cached
    if _cached is None:
        _cached = load_settings()
    return _cached


def reset_settings() -> None:
    global _cached
    _cached = None
