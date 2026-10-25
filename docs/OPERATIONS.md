# Mika StackChan - Guía Operativa Rápida

## 1. Entorno de compilación y herramientas en el Mac
El entorno ya está preparado en el Mac:
- **Directorio:** `/Users/jramirez/mika`
- **Entorno Virtual:** `.venv/` (contiene `platformio`, `esptool`, `pyserial`, `mpremote`).
- **M5Burner:**
  - Aplicación gráfica instalada en `/Applications/M5Burner.app` (con permisos de Gatekeeper liberados).
  - Herramienta CLI en `tools/m5burner_cli.py` para listar y descargar firmwares de M5Burner desde terminal sin abrir la interfaz gráfica.
  - Firmware oficial de fábrica (`StackChan-UserDemo_V1.5.1.bin`) descargado en `backups/firmwares/`.
- **ESPHome (Integración con Home Assistant):**
  - `esphome` instalado en macOS (`/opt/homebrew/bin/esphome`).
  - Configuración oficial de StackChan K151 en `esphome/stackchan.yaml` vinculada a `m5stack/esphome-yaml` (`examples/kit/stackchan-bsp.factory.yaml`).
  - Generador automático de secretos desde `config/secrets.local.json` en `tools/prepare_esphome_secrets.py`.
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

# Listar todos los firmwares de M5Burner para StackChan / CoreS3
make m5burner-list

# Descargar un firmware de M5Burner por nombre
python3 tools/m5burner_cli.py download "ESP-Claw For CoreS3"

# Flashear el firmware oficial de fábrica de StackChan (V1.5.1)
make flash-factory

# Compilar o flashear ESPHome (para integrar con Home Assistant en martiricos)
make esphome-compile
make esphome-flash
make esphome-ota
```

## 3. Conversión de audios nuevos al formato del robot
Para añadir nuevos efectos de sonido (de Telegram, WhatsApp, MP3 o WAV) al banco local `data/sfx/`:

```bash
./tools/audio_to_pcm24k.sh mi_audio.ogg data/sfx/nuevo_efecto_24k.pcm
make fs
```

## 4. Configuración y secretos (`config/secrets.local.json`) y Portal Wi-Fi con QR
La configuración activa del dispositivo se gestiona en `config/secrets.local.json` (ignorado por Git):
- `wifi_ssid`: SSID de la red Wi-Fi principal
- `wifi_password`: Contraseña de la red
- `wifi_networks`: Lista priorizada de redes conocidas
- `gemini_api_key`: Clave de API de Gemini
- `config_csv_url`: URL del CSV de prompts y configuración

Para regenerar y subir `data/secrets.json` por USB:
```bash
make fs
```

### Cambiar la Wi-Fi sin cables (Paso 2.1: Portal Cautivo + QR)
- **Activación automática:** Si al encender el robot ninguna de las redes guardadas está disponible, Mika arranca automáticamente el punto de acceso **`Mika-Setup`** y muestra un **código QR** en pantalla.
- **Activación manual en cualquier momento:** Haz **doble toque en la esquina superior izquierda** de la pantalla mientras Mika está en reposo (`Idle`).
- **Proceso desde el móvil:**
  1. Escanea el QR de la pantalla (te conecta a la Wi-Fi `Mika-Setup`).
  2. Se abrirá el portal cautivo (o entra en `http://192.168.4.1`).
  3. Toca una de las redes detectadas, escribe la contraseña y pulsa **Guardar y Conectar**.
  4. La nueva red queda guardada con máxima prioridad en `/secrets.json` dentro de `LittleFS`.

### Cambiar la Wi-Fi enseñándole un QR a la cámara del robot (Paso 2.2: Cámara QR)
- **Cómo activarlo:**
  - **En cualquier momento (en reposo):** Haz **doble toque en la esquina superior derecha** de la pantalla de Mika.
  - **Desde el Portal de Configuración Wi-Fi:** Pulsa el botón azul **`LEER QR CAMARA`** en la pantalla del portal.
- **Formatos soportados (`parseWifiPayloadString` + librería `lib/quirc`):**
  - El código QR estándar de **"Compartir Wi-Fi"** de iOS y Android (`WIFI:S:<SSID>;T:<WPA|WEP|nopass>;P:<PASSWORD>;;`).
  - JSON (`{"ssid":"...","password":"..."}`) o texto directo `ssid:password`.
- **Proceso:**
  1. En tu móvil, abre los ajustes de tu red Wi-Fi y pulsa **Compartir / Mostrar código QR**.
  2. Pon la pantalla del móvil frente a la cámara de Mika (dentro del recuadro verde en pantalla).
  3. Al detectarlo (soporta imagen normal y espejada automáticamente con `quirc_flip`), Mika guarda la red en `/secrets.json`, se conecta y reproduce el sonido de confirmación (`Tada`).

## 5. Control Remoto por HTTP y Bot de Telegram `@Martiricos_bot` (Paso 3.1)
Mika expone un servidor HTTP ligero (0% de impacto en batería en reposo) en el puerto `80` (`http://192.168.1.36` y `http://mika.local` vía mDNS):

### Endpoints HTTP en Mika (`src/main.cpp`)
- `GET /api/status`: Devuelve estado en vivo (IP, SSID, RSSI, batería, memoria, si está ocupado y última frase hablada).
- `POST /api/speak` (o `GET /api/speak?text=...&mode=say|ask&gesture=...`):
  - `mode="say"` (por defecto): Mika dice en voz alta exactamente el texto enviado con su voz `Puck` en Gemini Live, moviendo la boca y los servos.
  - `mode="ask"`: Mika responde o improvisa sobre el mensaje con su personalidad completa de **Chicui**.
- `POST /api/action` (o `GET /api/action?type=...&yaw=...&pitch=...`):
  - Dispara gestos, expresiones y efectos locales: `chiquito`, `dance`, `giggle`, `happy`, `sad`, `angry`, `surprised`, `dizzy`, `tada`, `nod`, `shake`, `tilt`, `center`, `left`, `right`, `up`, `down`.

### Uso desde CLI (`tools/mika_client.py`)
```bash
python3 tools/mika_client.py status
python3 tools/mika_client.py say "Hola familia, a cenar"
python3 tools/mika_client.py ask "Cuéntame un chiste corto"
python3 tools/mika_client.py action chiquito
python3 tools/mika_client.py action dance
```

### Uso desde Telegram (`@Martiricos_bot`)
- **En lenguaje natural o nota de voz**:
  - *"Dile por Mika a los niños que bajen a cenar"*
  - *"Haz que Mika haga el Chiquito"* / *"Que Mika baile"*
  - *"Cómo está Mika de batería"*
- **Con comandos directos**:
  - `/decir Hola familia`
  - `/mika pregunta Cuéntame un chiste`
  - `/chiquito` o `/mika chiquito`
  - `/bailar` o `/mika baile`
  - `/mika estado`

## 6. Notas sobre compatibilidad y drivers (ESP-IDF v5 / Arduino 3.x)
- **M5Unified:** Fijada en `platformio.ini` a `0.2.14` para evitar roturas de API en `PY32IOExpander`.
- **Driver I2S:** En ESP-IDF v5, `I2sRingAudioPlayer` en `src/main.cpp` utiliza la API estándar `driver/i2s_std.h` (`i2s_new_channel`, `i2s_channel_init_std_mode`, `i2s_channel_write`). No mezclar con `driver/i2s.h` legacy.
