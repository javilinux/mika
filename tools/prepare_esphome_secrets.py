#!/usr/bin/env python3
"""Generate esphome/secrets.yaml from config/secrets.local.json."""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCAL_SECRETS = ROOT / "config" / "secrets.local.json"
ESPHOME_SECRETS = ROOT / "esphome" / "secrets.yaml"


def main():
    if not LOCAL_SECRETS.exists():
        print(f"Error: {LOCAL_SECRETS} not found.", file=sys.stderr)
        sys.exit(1)

    data = json.loads(LOCAL_SECRETS.read_text(encoding="utf-8"))
    ssid = data.get("wifi_ssid", "")
    password = data.get("wifi_password", "")

    ESPHOME_SECRETS.parent.mkdir(parents=True, exist_ok=True)
    content = (
        f'# Auto-generated from config/secrets.local.json\n'
        f'wifi_ssid: "{ssid}"\n'
        f'wifi_password: "{password}"\n'
    )
    ESPHOME_SECRETS.write_text(content, encoding="utf-8")
    print(f"OK: Generated {ESPHOME_SECRETS} (SSID: {ssid})")


if __name__ == "__main__":
    main()
