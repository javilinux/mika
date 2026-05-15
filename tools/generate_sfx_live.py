#!/usr/bin/env python3
import argparse
import base64
import json
import struct
import time
import wave
from pathlib import Path

import websocket


ROOT = Path(__file__).resolve().parents[1]
SECRETS_PATH = ROOT / "data" / "secrets.json"
OUT_DIR = ROOT / "data" / "sfx"
MODEL = "models/gemini-3.1-flash-live-preview"
SAMPLE_RATE = 24000


CLIPS = {
    "ouch": {
        "file": "ouch_24k",
        "text": (
            "Produce solo un quejido corto de robot adorable en espanol: '¡auch!'. "
            "Debe sonar como StackChan, pequeno y simpatico, no dramatico, menos de medio segundo. "
            "No digas ninguna otra palabra."
        ),
    },
    "happy": {
        "file": "happy_mmm_24k",
        "text": (
            "Produce solo un sonido corto de gustito de robot adorable al recibir una caricia: 'mmm'. "
            "Tono contento, suave y simpatico, menos de un segundo. No digas ninguna otra palabra."
        ),
    },
    "giggle": {
        "file": "giggle_24k",
        "text": (
            "Produce solo una risita muy corta de robot adorable: 'jeje'. "
            "Tono jugueton y pequeno, menos de un segundo. No digas ninguna otra palabra."
        ),
    },
}


def load_key() -> str:
    data = json.loads(SECRETS_PATH.read_text())
    key = data.get("gemini_api_key", "")
    if not key:
        raise RuntimeError("Missing gemini_api_key in data/secrets.json")
    return key


def signed_samples(raw: bytes) -> list[int]:
    count = len(raw) // 2
    if count == 0:
        return []
    return list(struct.unpack("<" + "h" * count, raw[: count * 2]))


def samples_to_bytes(samples: list[int]) -> bytes:
    if not samples:
        return b""
    return struct.pack("<" + "h" * len(samples), *samples)


def trim_and_normalize(raw: bytes, max_ms: int) -> bytes:
    samples = signed_samples(raw)
    if not samples:
        return raw

    threshold = 450
    first = 0
    last = len(samples) - 1
    while first < len(samples) and abs(samples[first]) < threshold:
        first += 1
    while last > first and abs(samples[last]) < threshold:
        last -= 1

    pad = int(SAMPLE_RATE * 0.035)
    first = max(0, first - pad)
    last = min(len(samples) - 1, last + pad)
    samples = samples[first : last + 1]

    max_samples = int(SAMPLE_RATE * max_ms / 1000)
    if len(samples) > max_samples:
        samples = samples[:max_samples]

    peak = max((abs(x) for x in samples), default=0)
    if peak > 0:
        target = 22000
        gain = min(4.0, target / peak)
        samples = [max(-32768, min(32767, int(x * gain))) for x in samples]

    fade_len = min(int(SAMPLE_RATE * 0.025), len(samples) // 5)
    for i in range(fade_len):
        factor = i / fade_len
        samples[i] = int(samples[i] * factor)
        samples[-i - 1] = int(samples[-i - 1] * factor)

    return samples_to_bytes(samples)


def write_wav(path: Path, raw: bytes) -> None:
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(raw)


def connect(api_key: str):
    url = (
        "wss://generativelanguage.googleapis.com/ws/"
        "google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
        f"?key={api_key}"
    )
    return websocket.create_connection(url, timeout=20)


def send_json(ws, payload: dict) -> None:
    ws.send(json.dumps(payload, separators=(",", ":")))


def wait_setup(ws) -> None:
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        data = json.loads(ws.recv())
        if "setupComplete" in data:
            return
    raise RuntimeError("setupComplete timeout")


def generate_clip(api_key: str, prompt: str, voice: str) -> bytes:
    ws = connect(api_key)
    try:
        setup = {
            "setup": {
                "model": MODEL,
                "generationConfig": {
                    "responseModalities": ["AUDIO"],
                    "speechConfig": {
                        "voiceConfig": {
                            "prebuiltVoiceConfig": {
                                "voiceName": voice,
                            }
                        }
                    },
                },
                "systemInstruction": {
                    "parts": [
                        {
                            "text": (
                                "Eres un generador de efectos de sonido para un robot pequeno. "
                                "Devuelve solo el sonido pedido. No anadas explicaciones ni frases extra."
                            )
                        }
                    ]
                },
                "outputAudioTranscription": {},
            }
        }
        send_json(ws, setup)
        wait_setup(ws)
        send_json(ws, {"realtimeInput": {"text": prompt}})

        raw = bytearray()
        transcript = ""
        last_audio_at = time.monotonic()
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            ws.settimeout(3)
            try:
                data = json.loads(ws.recv())
            except TimeoutError:
                if raw and time.monotonic() - last_audio_at > 1.0:
                    break
                continue
            except websocket.WebSocketTimeoutException:
                if raw and time.monotonic() - last_audio_at > 1.0:
                    break
                continue

            server = data.get("serverContent", {})
            turn = server.get("modelTurn", {})
            for part in turn.get("parts", []):
                inline = part.get("inlineData", {})
                if inline.get("mimeType", "").startswith("audio/pcm") and inline.get("data"):
                    raw.extend(base64.b64decode(inline["data"]))
                    last_audio_at = time.monotonic()
            out = server.get("outputTranscription", {})
            if out.get("text"):
                transcript += out["text"]
            if server.get("turnComplete"):
                break

        if transcript:
            print(f"transcript: {transcript[:80]}")
        return bytes(raw)
    finally:
        ws.close()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--voice", default="Puck")
    parser.add_argument("--clip", choices=sorted(CLIPS), action="append")
    parser.add_argument("--max-ms", type=int, default=900)
    args = parser.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    api_key = load_key()
    names = args.clip or list(CLIPS)
    for name in names:
        spec = CLIPS[name]
        print(f"generating {name} with {args.voice}")
        raw = generate_clip(api_key, spec["text"], args.voice)
        raw = trim_and_normalize(raw, args.max_ms)
        pcm_path = OUT_DIR / f"{spec['file']}.pcm"
        wav_path = OUT_DIR / f"{spec['file']}.wav"
        pcm_path.write_bytes(raw)
        write_wav(wav_path, raw)
        print(f"wrote {pcm_path.relative_to(ROOT)} {len(raw) // 2} samples")


if __name__ == "__main__":
    main()
