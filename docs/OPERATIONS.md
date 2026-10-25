# Mika StackChan - Guía Operativa Rápida

## 1. Entorno de compilación y herramientas en el Mac
El entorno ya está preparado en el Mac:
- **Directorio:** `/Users/jramirez/mika`
- **Entorno Virtual:** `.venv/` (contiene `platformio`, `esptool`, `pyserial`, `mpremote`).
- **Herramientas de Audio:** `ffmpeg` y `sox` instalados globalmente (`/opt/homebrew/bin/`).
- **Dispositivo USB:** `/dev/cu.usbmodem1201` (CoreS3 con ESP32-S3, 16 MB Flash, 8 MB PSRAM).

## 2. Comandos directos (`Makefile`)
Desde la carpeta raíz del proyecto (`/Users/jramirez/mika`) puedes ejecutar:

```bash
# Compilar firmware
make build

# Subir firmware al robot
make flash

# Preparar y subir la configuración/LittleFS (WiFi, API key, audios, etc.)
make fs

# Abrir el monitor serie
make monitor

# Guardar snapshot rápido de las particiones compiladas (bootloader + partitions + firmware + littlefs)
make snapshot

# Restaurar en ~15 segundos el snapshot funcional de Mika (ideal tras probar otros firmwares)
make restore

# Volcar los 16 MB completos de la memoria Flash del robot a backups/mika_full_16MB.bin
make backup-full

# Restaurar la imagen completa de 16 MB
make restore-full
```

## 3. Conversión de audios nuevos al formato del robot
Para añadir nuevos efectos de sonido (de Telegram, WhatsApp, MP3 o WAV) al banco local `data/sfx/`:

```bash
./tools/audio_to_pcm24k.sh mi_audio.ogg data/sfx/nuevo_efecto_24k.pcm
make fs
```

## 4. Configuración y secretos (`config/secrets.local.json`)
La configuración activa del dispositivo se gestiona en `config/secrets.local.json` (ignorado por Git):
- `wifi_ssid`: SSID de la red Wi-Fi principal
- `wifi_password`: Contraseña de la red
- `wifi_networks`: Lista priorizada de redes conocidas
- `gemini_api_key`: Clave de API de Gemini
- `config_csv_url`: URL del CSV de prompts y configuración

Para regenerar y subir `data/secrets.json`:
```bash
make fs
```

## 5. Notas sobre compatibilidad y drivers (ESP-IDF v5 / Arduino 3.x)
- **M5Unified:** Fijada en `platformio.ini` a `0.2.14` para evitar roturas de API en `PY32IOExpander`.
- **Driver I2S:** En ESP-IDF v5, `I2sRingAudioPlayer` en `src/main.cpp` utiliza la API estándar `driver/i2s_std.h` (`i2s_new_channel`, `i2s_channel_init_std_mode`, `i2s_channel_write`). No mezclar con `driver/i2s.h` legacy.
