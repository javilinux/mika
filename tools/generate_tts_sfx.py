#!/usr/bin/env python3
import argparse
import base64
import json
import struct
import urllib.error
import urllib.request
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SECRETS_PATH = ROOT / "data" / "secrets.json"
OUT_DIR = ROOT / "data" / "sfx"
PREVIEW_DIR = ROOT / "generated" / "sfx_preview"
MODEL = "gemini-2.5-flash-preview-tts"
VOICE = "Puck"
SAMPLE_RATE = 24000


CLIPS = {
    "dizzy_mareao": {
        "file": "dizzy_mareao_24k",
        "max_ms": 0,
        "text": (
            "Say in Spanish from Malaga, Andalusian accent, upbeat male Puck voice, "
            "cute robot, very dizzy and funny, with a wobbly start: "
            "\"¡Uy, uy, uy... me he mareao una mijilla!\" "
            "Keep it under 1.5 seconds. No extra words."
        ),
    },
    "dizzy_meneito": {
        "file": "dizzy_meneito_24k",
        "max_ms": 0,
        "text": (
            "Say in Spanish from Malaga, Andalusian accent, upbeat male Puck voice, "
            "cute robot, surprised and dizzy, theatrical but friendly: "
            "\"¡Madre mia, que meneito me has dao!\" "
            "Keep it under 1.5 seconds. No extra words."
        ),
    },
    "dizzy_bits": {
        "file": "dizzy_bits_24k",
        "max_ms": 0,
        "text": (
            "Say in Spanish from Malaga, Andalusian accent, upbeat male Puck voice, "
            "cute robot, dizzy and slightly panicked, with comic timing: "
            "\"¡Esperate, que se me han movio los bits!\" "
            "Keep it under 1.5 seconds. No extra words."
        ),
    },
    "dizzy_firewalls": {
        "file": "dizzy_firewalls_24k",
        "max_ms": 0,
        "text": (
            "Say in Spanish from Malaga, Andalusian accent, upbeat male Puck voice, "
            "cute robot, very dizzy, amused and dramatic: "
            "\"¡Estoy viendo firewalls dobles!\" "
            "Keep it under 1.4 seconds. No extra words."
        ),
    },
    "tilt_cuidao": {
        "file": "tilt_cuidao_24k",
        "max_ms": 0,
        "text": (
            "Say in Spanish from Malaga, Andalusian accent, upbeat male Puck voice, "
            "cute robot, alarmed but funny, start with a rapid warning: "
            "\"¡Eh, eh, eh! ¡Cuidao, que me voy!\" "
            "Keep it under 1.4 seconds. No extra words."
        ),
    },
    "tilt_derechito": {
        "file": "tilt_derechito_24k",
        "max_ms": 0,
        "text": (
            "Say in Spanish from Malaga, Andalusian accent, upbeat male Puck voice, "
            "cute robot, friendly complaint, funny and expressive: "
            "\"¡Illo, ponme derechito!\" "
            "Keep it under 1.2 seconds. No extra words."
        ),
    },
    "tilt_wifi": {
        "file": "tilt_wifi_24k",
        "max_ms": 0,
        "text": (
            "Say in Spanish from Malaga, Andalusian accent, upbeat male Puck voice, "
            "cute robot, comic alarm, a bit exaggerated: "
            "\"¡Ay, que se me cae el Wi-Fi!\" "
            "Keep it under 1.3 seconds. No extra words."
        ),
    },
    "tilt_desconfiguro": {
        "file": "tilt_desconfiguro_24k",
        "max_ms": 0,
        "text": (
            "Say in Spanish from Malaga, Andalusian accent, upbeat male Puck voice, "
            "cute robot, playful warning, energetic: "
            "\"¡Quieto parao, que me desconfiguro!\" "
            "Keep it under 1.5 seconds. No extra words."
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

    threshold = 80
    first = 0
    last = len(samples) - 1
    while first < len(samples) and abs(samples[first]) < threshold:
        first += 1
    while last > first and abs(samples[last]) < threshold:
        last -= 1

    pad = int(SAMPLE_RATE * 0.12)
    first = max(0, first - pad)
    last = min(len(samples) - 1, last + pad)
    samples = samples[first : last + 1]

    if max_ms > 0:
        max_samples = int(SAMPLE_RATE * max_ms / 1000)
        if len(samples) > max_samples:
            samples = samples[:max_samples]

    peak = max((abs(x) for x in samples), default=0)
    if peak > 0:
        target = 22000
        gain = min(3.5, target / peak)
        samples = [max(-32768, min(32767, int(x * gain))) for x in samples]

    fade_len = min(int(SAMPLE_RATE * 0.025), len(samples) // 5)
    for i in range(fade_len):
        factor = i / fade_len
        samples[i] = int(samples[i] * factor)
        samples[-i - 1] = int(samples[-i - 1] * factor)

    samples.extend([0] * int(SAMPLE_RATE * 0.45))
    return samples_to_bytes(samples)


def write_wav(path: Path, raw: bytes) -> None:
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(raw)


def generate_clip(api_key: str, prompt: str) -> bytes:
    url = f"https://generativelanguage.googleapis.com/v1beta/models/{MODEL}:generateContent"
    payload = {
        "contents": [{"parts": [{"text": prompt}]}],
        "generationConfig": {
            "responseModalities": ["AUDIO"],
            "speechConfig": {
                "voiceConfig": {
                    "prebuiltVoiceConfig": {
                        "voiceName": VOICE,
                    }
                }
            },
        },
        "model": MODEL,
    }
    request = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "x-goog-api-key": api_key,
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=45) as response:
            data = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"TTS HTTP {exc.code}: {body[:500]}") from exc

    inline_data = data["candidates"][0]["content"]["parts"][0]["inlineData"]["data"]
    return base64.b64decode(inline_data)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("clips", nargs="*", choices=sorted(CLIPS.keys()))
    args = parser.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)

    api_key = load_key()
    clip_names = args.clips or sorted(CLIPS.keys())
    for name in clip_names:
        spec = CLIPS[name]
        raw = generate_clip(api_key, spec["text"])
        raw = trim_and_normalize(raw, spec.get("max_ms", 1500))
        pcm_path = OUT_DIR / f"{spec['file']}.pcm"
        wav_path = PREVIEW_DIR / f"{spec['file']}.wav"
        pcm_path.write_bytes(raw)
        write_wav(wav_path, raw)
        duration_ms = len(raw) // 2 * 1000 // SAMPLE_RATE
        print(f"{name}: {pcm_path} {duration_ms} ms")


if __name__ == "__main__":
    main()
