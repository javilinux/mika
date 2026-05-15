#!/usr/bin/env python3
import argparse
import csv
import io
import json
import sys
import time
from pathlib import Path

import requests


EXPECTED_COLUMNS = ["section", "key", "value", "enabled", "order"]
CONFIG_PATH = Path("config/secrets.local.json")


def fail(message):
    print(f"FAIL: {message}")
    return 1


def check(condition, message):
    print(f"{'OK' if condition else 'FAIL'}: {message}")
    return bool(condition)


def live_websocket_check(api_key, model):
    try:
        import websocket
    except Exception as exc:
        print(f"FAIL: websocket-client is not available: {exc}")
        return False

    model_name = model if model.startswith("models/") else f"models/{model}"
    url = (
        "wss://generativelanguage.googleapis.com/ws/"
        "google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
        f"?key={api_key}"
    )

    ws = websocket.create_connection(url, timeout=20)
    ws.settimeout(12)
    try:
        ws.send(
            json.dumps(
                {
                    "setup": {
                        "model": model_name,
                        "generationConfig": {"responseModalities": ["AUDIO"]},
                        "systemInstruction": {
                            "parts": [
                                {
                                    "text": (
                                        "Responde siempre en espanol, breve. "
                                        "Si te piden decir una frase exacta, di solo esa frase."
                                    )
                                }
                            ]
                        },
                        "outputAudioTranscription": {},
                    }
                }
            )
        )
        setup = json.loads(ws.recv())
        if "setupComplete" not in setup:
            print("FAIL: Live setup did not complete")
            return False

        ws.send(json.dumps({"realtimeInput": {"text": "Di exactamente: prueba correcta."}}))

        audio_chunks = 0
        transcript = []
        deadline = time.time() + 25
        while time.time() < deadline:
            raw = ws.recv()
            if not raw:
                break
            message = json.loads(raw)
            server_content = message.get("serverContent") or {}
            output_transcription = server_content.get("outputTranscription") or {}
            if output_transcription.get("text"):
                transcript.append(output_transcription["text"])
            model_turn = server_content.get("modelTurn") or {}
            for part in model_turn.get("parts") or []:
                if part.get("inlineData", {}).get("data"):
                    audio_chunks += 1
            if server_content.get("turnComplete"):
                break

        ok = audio_chunks > 0 and bool(transcript)
        print(f"{'OK' if ok else 'FAIL'}: Live WebSocket audio/text turn")
        print("live_audio_chunks:", audio_chunks)
        print("live_transcript_received:", bool(transcript))
        if transcript:
            print("live_transcript_sample:", "".join(transcript)[:120])
        return ok
    finally:
        ws.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--live", action="store_true", help="also test Gemini Live WebSocket")
    args = parser.parse_args()

    if not CONFIG_PATH.exists():
        return fail(f"{CONFIG_PATH} does not exist")

    data = json.loads(CONFIG_PATH.read_text())
    ok = True

    ok &= check(bool(data.get("gemini_api_key")), "gemini_api_key is set")
    ok &= check(bool(data.get("config_csv_url")), "config_csv_url is set")

    networks = data.get("wifi_networks") or []
    ok &= check(isinstance(networks, list) and len(networks) >= 1, "wifi_networks is present")
    if networks:
        ordered = sorted(networks, key=lambda n: int(n.get("priority", 1000)))
        print("wifi_order:", " -> ".join(n.get("ssid", "<missing>") for n in ordered))
        ok &= check(ordered[0].get("ssid") == "GoogleGuest", "production Wi-Fi is first")
        ok &= check(any(n.get("ssid") == "Google-Guest" for n in ordered), "Google-Guest fallback is present")

    if data.get("config_csv_url"):
        response = requests.get(data["config_csv_url"], timeout=20)
        ok &= check(response.status_code == 200, f"CSV HTTP status is {response.status_code}")
        content_type = response.headers.get("content-type", "").split(";")[0]
        print("csv_content_type:", content_type or "<missing>")

        csv_text = response.content.decode(response.encoding or "utf-8-sig", errors="replace")
        reader = csv.DictReader(io.StringIO(csv_text))
        rows = list(reader)
        ok &= check(reader.fieldnames == EXPECTED_COLUMNS, "CSV schema is section,key,value,enabled,order")
        ok &= check(len(rows) > 0, f"CSV has {len(rows)} rows")

        enabled_rows = [row for row in rows if row.get("enabled", "").lower() == "yes"]
        ok &= check(len(enabled_rows) > 0, f"CSV has {len(enabled_rows)} enabled rows")

        for index, row in enumerate(rows, start=2):
            try:
                int(row.get("order", ""))
            except ValueError:
                print(f"FAIL: CSV line {index} has invalid order")
                ok = False

        settings = {
            row["key"]: row["value"]
            for row in enabled_rows
            if row.get("section") == "settings"
        }
        model = settings.get("model")
        ok &= check(bool(model), "settings.model is present")
        if model:
            print("settings_model:", model)

    if data.get("gemini_api_key"):
        response = requests.get(
            "https://generativelanguage.googleapis.com/v1beta/models",
            params={"key": data["gemini_api_key"]},
            timeout=20,
        )
        ok &= check(response.status_code == 200, f"Gemini models HTTP status is {response.status_code}")
        if response.status_code == 200:
            models = [model.get("name", "") for model in response.json().get("models", [])]
            configured = None
            if "settings" in locals():
                configured = settings.get("model")
            if configured:
                model_name = configured if configured.startswith("models/") else f"models/{configured}"
                ok &= check(model_name in models, f"configured Gemini model is listed: {model_name}")
            print("gemini_models_count:", len(models))
        else:
            try:
                error = response.json().get("error", {})
                print("gemini_error_status:", error.get("status"))
                print("gemini_error_message:", (error.get("message") or "")[:180])
            except Exception:
                print("gemini_error_text:", response.text[:180])

    if args.live:
        if not data.get("gemini_api_key"):
            ok = False
            print("FAIL: cannot run Live check without gemini_api_key")
        elif "settings" not in locals() or not settings.get("model"):
            ok = False
            print("FAIL: cannot run Live check without settings.model")
        else:
            ok &= live_websocket_check(data["gemini_api_key"], settings["model"])

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
