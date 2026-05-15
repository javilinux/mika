#!/usr/bin/env python3
import json
from pathlib import Path


SOURCE = Path("config/secrets.local.json")
TARGET = Path("data/secrets.json")


def main():
    if not SOURCE.exists():
        raise SystemExit(f"Missing {SOURCE}")

    data = json.loads(SOURCE.read_text())
    required = ["gemini_api_key", "config_csv_url"]
    missing = [key for key in required if not data.get(key)]
    if missing:
        raise SystemExit(f"Missing required field(s): {', '.join(missing)}")

    networks = data.get("wifi_networks") or []
    if not networks:
        ssid = data.get("wifi_ssid", "")
        if not ssid:
            raise SystemExit("Missing wifi_networks or wifi_ssid")
        password = data.get("wifi_password", "")
        networks = [
            {
                "ssid": ssid,
                "password": password,
                "auth": "wpa" if password else "open",
                "priority": 100,
            }
        ]

    output = {
        "gemini_api_key": data["gemini_api_key"],
        "config_csv_url": data["config_csv_url"],
        "wifi_networks": sorted(networks, key=lambda item: int(item.get("priority", 1000))),
        "log_secrets": False,
    }

    TARGET.parent.mkdir(parents=True, exist_ok=True)
    TARGET.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n")
    TARGET.chmod(0o600)
    print(f"Wrote {TARGET} without printing secrets.")
    print("Wi-Fi order:", " -> ".join(item["ssid"] for item in output["wifi_networks"]))


if __name__ == "__main__":
    main()

