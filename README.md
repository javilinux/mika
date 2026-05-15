# Mika StackChan GSEC

Firmware for a StackChan CoreS3 robot used as an AI host for GSEC Malaga demos.

## Features

- Google Sheet CSV configuration loaded at boot and by double tapping the top of the screen.
- Gemini Live WebSocket integration with local API key storage.
- I2S/DMA ring-buffer playback for low stutter audio.
- Touch-to-talk using the top touch sensor.
- Minimal StackChan-style face with blinking, gaze, lip-sync, expressions and touch reactions.
- Screen interactions for eyes, cheeks, mouth, CSV reload and dance.
- Local PCM sound effects and dance music stored in LittleFS.
- Conversation context across turns.

## Local Secrets

Do not commit real credentials.

Create `config/secrets.local.json` from `config/secrets.example.json`, then generate `data/secrets.json` with:

```bash
python3 tools/prepare_device_fs.py
```

The ignored files are:

- `config/secrets.local.json`
- `data/secrets.json`

## Build

```bash
python3 -m venv .venv
.venv/bin/pip install platformio
.venv/bin/pio run
```

## Upload

```bash
.venv/bin/pio run -t upload
.venv/bin/pio run -t uploadfs
```

## Monitor

```bash
.venv/bin/pio device monitor -p /dev/ttyACM0 -b 115200
```

## CSV Notes

The firmware expects a simple CSV with these columns:

```csv
section,key,value,enabled,order
```

Do not use multiline cells. Use quoted values for commas, and split long prompt text into multiple rows.
