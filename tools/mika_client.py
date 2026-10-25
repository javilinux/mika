#!/usr/bin/env python3
"""
Cliente HTTP local (sin dependencias externas) para controlar el robot M5Stack StackChan (Mika / Chicui).
Soporta:
  - status: estado en vivo (IP, Wi-Fi, RSSI, batería, memoria, última frase)
  - speak: hablar con voz de Gemini Live (Puck) + sincronización labial + gesto opcional
  - action: ejecutar gestos de servos, expresiones faciales y efectos locales (chiquito, dance, giggle, etc.)
"""

import os
import sys
import json
import urllib.request
import urllib.error
from typing import Dict, Any, Optional

DEFAULT_HOSTS = [
    os.environ.get("MIKA_HOST", "").strip(),
    "192.168.1.36",
    "mika.local",
]


def _candidate_urls(path: str):
    seen = set()
    for h in DEFAULT_HOSTS:
        if not h or h in seen:
            continue
        seen.add(h)
        base = h if h.startswith("http") else f"http://{h}"
        yield f"{base.rstrip('/')}/{path.lstrip('/')}"


def _http_json(url: str, method: str = "GET", payload: Optional[Dict[str, Any]] = None, timeout: float = 3.5) -> Dict[str, Any]:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json; charset=utf-8"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace")
        return json.loads(raw)


def get_mika_status(timeout: float = 3.0) -> Dict[str, Any]:
    last_err = "No se pudo conectar con Mika"
    for url in _candidate_urls("/api/status"):
        try:
            return _http_json(url, method="GET", timeout=timeout)
        except Exception as e:
            last_err = str(e)
    return {"ok": False, "error": last_err}


def mika_speak(text: str, mode: str = "say", gesture: str = "", timeout: float = 4.0) -> Dict[str, Any]:
    text = (text or "").strip()
    if not text:
        return {"ok": False, "error": "Texto vacío"}
    payload = {"text": text, "mode": mode or "say", "gesture": gesture or ""}
    last_err = "No se pudo conectar con Mika"
    for url in _candidate_urls("/api/speak"):
        try:
            return _http_json(url, method="POST", payload=payload, timeout=timeout)
        except urllib.error.HTTPError as e:
            try:
                return json.loads(e.read().decode("utf-8", errors="replace"))
            except Exception:
                last_err = f"HTTP {e.code}"
        except Exception as e:
            last_err = str(e)
    return {"ok": False, "error": last_err}


def mika_action(action_type: str = "", yaw: Optional[int] = None, pitch: Optional[int] = None, timeout: float = 3.5) -> Dict[str, Any]:
    payload: Dict[str, Any] = {}
    if action_type:
        payload["type"] = action_type.strip().lower()
    if yaw is not None:
        payload["yaw"] = int(yaw)
    if pitch is not None:
        payload["pitch"] = int(pitch)

    last_err = "No se pudo conectar con Mika"
    for url in _candidate_urls("/api/action"):
        try:
            return _http_json(url, method="POST", payload=payload, timeout=timeout)
        except urllib.error.HTTPError as e:
            try:
                return json.loads(e.read().decode("utf-8", errors="replace"))
            except Exception:
                last_err = f"HTTP {e.code}"
        except Exception as e:
            last_err = str(e)
    return {"ok": False, "error": last_err}


def format_mika_status_html(st: Dict[str, Any]) -> str:
    if not st.get("ok"):
        return f"⚠️ <b>Mika no responde en la red local</b>\n<code>{st.get('error', 'Desconectado')}</code>"
    charging = "⚡ Cargando" if st.get("is_charging") else "🔋 Batería"
    busy = "🗣️ Hablando/Ocupado" if st.get("busy") else "🟢 En reposo (Idle)"
    msg = (
        f"🤖 <b>Estado de Mika (StackChan)</b>\n"
        f"• <b>Estado:</b> {busy}\n"
        f"• <b>Energía:</b> {st.get('battery_level', '?')}% ({charging})\n"
        f"• <b>Wi-Fi:</b> <code>{st.get('ssid', '?')}</code> ({st.get('rssi', '?')} dBm)\n"
        f"• <b>IP Local:</b> <code>{st.get('ip', '?')}</code>\n"
        f"• <b>Uptime:</b> {int(st.get('uptime_s', 0)) // 60} min\n"
    )
    if st.get("last_output"):
        msg += f"• <b>Última frase:</b> <i>\"{st['last_output'][:180]}\"</i>"
    return msg


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    if cmd == "status":
        print(json.dumps(get_mika_status(), indent=2, ensure_ascii=False))
    elif cmd in ("say", "speak"):
        txt = " ".join(sys.argv[2:]) or "Hola familia, soy Mika"
        print(json.dumps(mika_speak(txt, mode="say"), indent=2, ensure_ascii=False))
    elif cmd == "ask":
        txt = " ".join(sys.argv[2:]) or "Cuéntame un chiste corto"
        print(json.dumps(mika_speak(txt, mode="ask"), indent=2, ensure_ascii=False))
    elif cmd == "action":
        act = sys.argv[2] if len(sys.argv) > 2 else "chiquito"
        print(json.dumps(mika_action(act), indent=2, ensure_ascii=False))
    else:
        print(json.dumps(mika_action(cmd), indent=2, ensure_ascii=False))
