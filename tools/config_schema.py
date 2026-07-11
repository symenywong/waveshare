from __future__ import annotations

import csv
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Mapping
from urllib.parse import urlparse


class ConfigValidationError(ValueError):
    """Raised when a provider configuration is unsafe or unsupported."""


@dataclass(frozen=True)
class ProviderDefinition:
    provider_id: str
    allowed_hosts: tuple[str, ...]
    allowed_models: tuple[str, ...]
    capabilities: Mapping[str, Any]
    allow_maas_workspace_hosts: bool = False


class ProviderCatalog:
    def __init__(self, providers: Mapping[str, ProviderDefinition]):
        self._providers = dict(providers)

    @classmethod
    def default(cls) -> "ProviderCatalog":
        return cls(
            {
                "dashscope_openai_chat": ProviderDefinition(
                    provider_id="dashscope_openai_chat",
                    allowed_hosts=(
                        "dashscope.aliyuncs.com",
                        "dashscope-intl.aliyuncs.com",
                        "dashscope-us.aliyuncs.com",
                        "cn-hongkong.dashscope.aliyuncs.com",
                    ),
                    allowed_models=("qwen3.7-max", "qwen-plus", "qwen-turbo"),
                    allow_maas_workspace_hosts=True,
                    capabilities={
                        "supports_chat_stream": True,
                        "supports_reasoning_controls": True,
                        "supports_data_uri_audio": False,
                        "requires_public_audio_url": False,
                        "async_transcription": False,
                    },
                ),
                "minimax_openai_chat": ProviderDefinition(
                    provider_id="minimax_openai_chat",
                    allowed_hosts=("api.minimax.io", "api.minimax.chat"),
                    allowed_models=("MiniMax-M3",),
                    capabilities={
                        "supports_chat_stream": True,
                        "supports_reasoning_controls": True,
                        "supports_data_uri_audio": False,
                        "requires_public_audio_url": False,
                        "async_transcription": False,
                    },
                ),
                "dashscope_qwen_asr_flash": ProviderDefinition(
                    provider_id="dashscope_qwen_asr_flash",
                    allowed_hosts=(
                        "dashscope.aliyuncs.com",
                        "dashscope-intl.aliyuncs.com",
                        "dashscope-us.aliyuncs.com",
                        "cn-hongkong.dashscope.aliyuncs.com",
                    ),
                    allowed_models=("qwen3-asr-flash",),
                    allow_maas_workspace_hosts=True,
                    capabilities={
                        "supports_chat_stream": False,
                        "supports_reasoning_controls": False,
                        "supports_data_uri_audio": True,
                        "requires_public_audio_url": False,
                        "async_transcription": False,
                        "max_audio_bytes": 10 * 1024 * 1024,
                        "max_audio_seconds": 5 * 60,
                    },
                ),
            }
        )

    def get(self, provider_id: str) -> ProviderDefinition:
        try:
            return self._providers[provider_id]
        except KeyError as exc:
            raise ConfigValidationError(f"Unsupported provider: {provider_id}") from exc


def redact_secret(secret: str) -> str:
    if len(secret) < 8:
        return "***"
    return f"{secret[:3]}***{secret[-4:]}"


def _host_allowed(host: str, definition: ProviderDefinition) -> bool:
    allowed_hosts = definition.allowed_hosts
    if host in allowed_hosts:
        return True
    return definition.allow_maas_workspace_hosts and host.endswith(".maas.aliyuncs.com")


def _utf8_len(value: str) -> int:
    return len(value.encode("utf-8"))


def validate_provider_config(catalog: ProviderCatalog, raw_config: Mapping[str, Any]) -> Dict[str, Any]:
    provider_id = str(raw_config.get("provider", "")).strip()
    definition = catalog.get(provider_id)

    base_url = str(raw_config.get("base_url", "")).strip()
    if _utf8_len(base_url) >= 160:
        raise ConfigValidationError("base_url is too long for device storage")
    parsed = urlparse(base_url)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ConfigValidationError("base_url must be an HTTPS URL")
    if not _host_allowed(parsed.hostname or "", definition):
        raise ConfigValidationError(f"base_url host is not approved for provider {provider_id}")

    model = str(raw_config.get("model", "")).strip()
    if _utf8_len(model) >= 64:
        raise ConfigValidationError("model is too long for device storage")
    if not model or any(char.isspace() for char in model):
        raise ConfigValidationError("model must be an API model ID without whitespace")
    if model not in definition.allowed_models:
        raise ConfigValidationError(f"model {model} is not approved for provider {provider_id}")

    api_key = str(raw_config.get("api_key", ""))
    if len(api_key) < 6:
        raise ConfigValidationError("api_key is missing or too short")
    if _utf8_len(api_key) >= 192:
        raise ConfigValidationError("api_key is too long for device storage")

    return {
        "provider": provider_id,
        "base_url": base_url.rstrip("/"),
        "model": model,
        "api_key_redacted": redact_secret(api_key),
        "capabilities": dict(definition.capabilities),
    }


def validate_wifi_config(ssid: str, password: str) -> Dict[str, str]:
    ssid = ssid.strip()
    if not ssid:
        raise ConfigValidationError("wifi_ssid is required for NVS provisioning")
    if _utf8_len(ssid) > 32:
        raise ConfigValidationError("wifi_ssid must be at most 32 bytes")

    if _utf8_len(password) > 63:
        raise ConfigValidationError("wifi_password must be at most 63 bytes")
    if password and len(password) < 8:
        raise ConfigValidationError("wifi_password must be empty or at least 8 characters")

    return {
        "wifi_ssid": ssid,
        "wifi_password": password,
    }


def build_nvs_rows(
    validated_provider: Mapping[str, Any],
    api_key: str,
    wifi_config: Mapping[str, str],
) -> list[list[str]]:
    return [
        ["key", "type", "encoding", "value"],
        ["aiqa", "namespace", "", ""],
        ["version", "data", "u32", "1"],
        ["provider", "data", "string", str(validated_provider["provider"])],
        ["model", "data", "string", str(validated_provider["model"])],
        ["base_url", "data", "string", str(validated_provider["base_url"])],
        ["stream", "data", "u8", "1"],
        ["hide_reason", "data", "u8", "1"],
        ["max_tokens", "data", "i32", "768"],
        ["wifi_ssid", "data", "string", wifi_config["wifi_ssid"]],
        ["wifi_pass", "data", "string", wifi_config["wifi_password"]],
        ["chat_key", "data", "string", api_key],
        ["asr_key", "data", "string", api_key],
    ]


def write_nvs_csv(path: Path, rows: list[list[str]]) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW

    try:
        fd = os.open(path, flags, 0o600)
    except FileExistsError as exc:
        raise ConfigValidationError(f"{path} already exists") from exc
    except OSError as exc:
        raise ConfigValidationError(f"failed to create {path}: {exc.strerror}") from exc

    with os.fdopen(fd, "w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerows(rows)
