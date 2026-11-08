#!/usr/bin/env python3
"""Generate a QWeather Ed25519 JWT and call the weather-now API."""

from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

try:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError as exc:
    raise SystemExit(
        "Missing dependency: cryptography\n"
        "Install it with: python -m pip install -r tools/requirements-qweather.txt"
    ) from exc


def base64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def load_private_key(path: Path) -> Ed25519PrivateKey:
    try:
        pem = path.read_bytes()
    except OSError as exc:
        raise ValueError(f"Cannot read private key {path}: {exc}") from exc

    try:
        key = serialization.load_pem_private_key(pem, password=None)
    except (TypeError, ValueError) as exc:
        raise ValueError(
            "The private key must be an unencrypted PKCS#8 PEM Ed25519 key"
        ) from exc

    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError("The supplied private key is not an Ed25519 key")
    return key


def build_jwt(kid: str, project_id: str, key: Ed25519PrivateKey) -> str:
    now = int(time.time())
    header = {"alg": "EdDSA", "kid": kid}
    payload = {
        "sub": project_id,
        "iat": now - 30,
        "exp": now + 870,
    }

    encoded_header = base64url(
        json.dumps(header, separators=(",", ":")).encode("utf-8")
    )
    encoded_payload = base64url(
        json.dumps(payload, separators=(",", ":")).encode("utf-8")
    )
    signing_input = f"{encoded_header}.{encoded_payload}".encode("ascii")
    signature = key.sign(signing_input)
    return f"{signing_input.decode('ascii')}.{base64url(signature)}"


def normalize_host(value: str) -> str:
    value = value.strip()
    parsed = urllib.parse.urlsplit(
        value if "://" in value else f"https://{value}"
    )
    if parsed.scheme != "https" or not parsed.hostname:
        raise ValueError("API host must be a valid HTTPS QWeather host")
    if parsed.port is not None or parsed.username is not None or parsed.password is not None:
        raise ValueError("API host must not contain credentials or a custom port")
    host = parsed.hostname.lower().rstrip(".")
    if not host.endswith(".qweatherapi.com"):
        raise ValueError("API host must end with .qweatherapi.com")
    return host


def request_weather(host: str, token: str, location: str, timeout: float) -> dict:
    query = urllib.parse.urlencode({"location": location, "lang": "zh"})
    url = f"https://{host}/v7/weather/now?{query}"
    request = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept-Encoding": "gzip",
            "User-Agent": "jcalendar-qweather-jwt/1.0",
        },
    )

    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read()
            if response.headers.get("Content-Encoding", "").lower() == "gzip":
                import gzip

                body = gzip.decompress(body)
            return json.loads(body.decode("utf-8"))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"QWeather returned HTTP {exc.code}: {body}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"Cannot connect to QWeather: {exc.reason}") from exc
    except json.JSONDecodeError as exc:
        raise RuntimeError("QWeather returned invalid JSON") from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a QWeather JWT and request current weather."
    )
    parser.add_argument("--kid", default=os.getenv("QWEATHER_KID"), help="Credential ID")
    parser.add_argument(
        "--project-id",
        default=os.getenv("QWEATHER_PROJECT_ID"),
        help="Project ID used as JWT sub",
    )
    parser.add_argument(
        "--private-key",
        type=Path,
        default=Path(os.environ["QWEATHER_PRIVATE_KEY_FILE"])
        if os.getenv("QWEATHER_PRIVATE_KEY_FILE")
        else None,
        help="Path to an Ed25519 PKCS#8 PEM private key",
    )
    parser.add_argument(
        "--host", default=os.getenv("QWEATHER_API_HOST"), help="Dedicated API Host"
    )
    parser.add_argument(
        "--location",
        default=os.getenv("QWEATHER_LOCATION", "101010100"),
        help="LocationID or longitude,latitude (default: Beijing)",
    )
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument(
        "--print-token", action="store_true", help="Print the complete JWT (sensitive)"
    )
    parser.add_argument(
        "--token-only", action="store_true", help="Generate a JWT without calling the API"
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    missing = [
        name
        for name, value in (
            ("--kid/QWEATHER_KID", args.kid),
            ("--project-id/QWEATHER_PROJECT_ID", args.project_id),
            ("--private-key/QWEATHER_PRIVATE_KEY_FILE", args.private_key),
        )
        if not value
    ]
    if not args.token_only and not args.host:
        missing.append("--host/QWEATHER_API_HOST")
    if missing:
        print("Missing required configuration: " + ", ".join(missing), file=sys.stderr)
        return 2

    try:
        key = load_private_key(args.private_key)
        token = build_jwt(args.kid.strip(), args.project_id.strip(), key)
        if args.print_token or args.token_only:
            print(token)
        else:
            print("JWT generated successfully (value hidden).")

        if args.token_only:
            return 0

        result = request_weather(
            normalize_host(args.host), token, args.location.strip(), args.timeout
        )
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if str(result.get("code")) == "200" else 1
    except (ValueError, RuntimeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
