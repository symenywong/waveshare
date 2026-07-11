#!/usr/bin/env python3
from __future__ import annotations

import argparse
import getpass
import json
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.config_schema import (
    ConfigValidationError,
    ProviderCatalog,
    build_nvs_rows,
    validate_provider_config,
    validate_wifi_config,
    write_nvs_csv,
)


FORBIDDEN_KEY_MESSAGE = "API keys must be provided through stdin or environment variables"


def reject_cli_api_key(argv: list[str]) -> None:
    for arg in argv:
        if arg == "--api-key" or arg.startswith("--api-key="):
            print(FORBIDDEN_KEY_MESSAGE, file=sys.stderr)
            raise SystemExit(2)


def parse_args(argv: list[str]) -> argparse.Namespace:
    reject_cli_api_key(argv)
    parser = argparse.ArgumentParser(description="Validate and stage AI Pet provider configuration.")
    parser.add_argument("--provider", required=True)
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--key-env", default="AIQA_API_KEY")
    parser.add_argument("--wifi-ssid", default="")
    parser.add_argument("--wifi-password", default="")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument(
        "--nvs-csv",
        type=Path,
        default=None,
        help="Write an ESP-IDF NVS CSV containing secrets. The file is chmod 0600.",
    )
    parser.add_argument(
        "--allow-plaintext-nvs",
        action="store_true",
        help="Allow generating a plaintext NVS CSV for development-only devices.",
    )
    return parser.parse_args(argv)


def read_api_key(env_name: str) -> str:
    env_value = os.environ.get(env_name)
    if env_value:
        return env_value

    if sys.stdin.isatty():
        return getpass.getpass("API key: ")

    return sys.stdin.read().strip()


def main(argv: list[str] | None = None) -> int:
    args = parse_args(list(sys.argv[1:] if argv is None else argv))
    api_key = read_api_key(args.key_env)

    try:
        validated = validate_provider_config(
            ProviderCatalog.default(),
            {
                "provider": args.provider,
                "base_url": args.base_url,
                "model": args.model,
                "api_key": api_key,
            },
        )
        wifi_config = None
        if args.nvs_csv is not None:
            if not args.allow_plaintext_nvs:
                raise ConfigValidationError(
                    "--nvs-csv writes plaintext secrets; pass --allow-plaintext-nvs for development-only use"
                )
            wifi_config = validate_wifi_config(args.wifi_ssid, args.wifi_password)
    except ConfigValidationError as exc:
        print(f"Invalid configuration: {exc}", file=sys.stderr)
        return 1

    output = {
        "provider": validated["provider"],
        "base_url": validated["base_url"],
        "model": validated["model"],
        "api_key": validated["api_key_redacted"],
        "capabilities": validated["capabilities"],
    }
    print(json.dumps(output, ensure_ascii=False, indent=2))

    if args.output is not None and not args.dry_run:
        args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        args.output.chmod(0o600)

    if args.nvs_csv is not None and not args.dry_run:
        assert wifi_config is not None
        try:
            write_nvs_csv(args.nvs_csv, build_nvs_rows(validated, api_key, wifi_config))
        except ConfigValidationError as exc:
            print(f"Invalid configuration: {exc}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
