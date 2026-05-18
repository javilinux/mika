# Mika StackChan GSEC

Firmware PlatformIO/Arduino para Mika, un StackChan M5Stack K151/CoreS3 usado como robot anfitrion de GSEC Malaga.

El objetivo actual es una experiencia local "robot-only": credenciales y configuracion en el robot, Google Sheet publicado como CSV para contenido editable, Gemini Live para conversacion en tiempo real, y reacciones visuales/fisicas locales para que el robot parezca vivo sin depender de infraestructura externa propia.

## Estado actual

- Conversacion por Gemini Live con voz `Puck`, audio en streaming y sesiones calientes reutilizables.
- Configuracion remota desde Google Sheet CSV, cache local y recarga con doble toque en la parte superior de la pantalla.
- Wi-Fi priorizado: produccion intenta `GoogleGuest` y `Google-Guest` abiertas; desarrollo cae a la red local configurada.
- Audio de baja latencia por I2S/DMA con ring buffer, prebuffer dinamico y metricas por turno.
- Modo robusto de escucha por toque: tocar arriba para empezar a grabar y tocar de nuevo para terminar.
- Cara minimalista StackChan con parpadeo, sacadas, respiracion, expresiones, lip-sync y reacciones tactiles.
- Gestos de servos para escucha, pensamiento, habla, negacion, afirmacion, sorpresa, baile y huevos de pascua.
- Banco local de SFX PCM en LittleFS para risas, pensamiento, sorpresa, Chiquito, IMU, tacto y baile.
- Memoria conversacional local y sesiones Live calientes para reducir latencia entre turnos.
- Reacciones IMU: mareo al agitar y queja al inclinar.

Funciones presentes en codigo pero desactivadas por decision practica:

- Presencia ambiental con LTR553: desactivada porque generaba sonidos idle no deseados.
- NFC diagnostics: desactivado; solo util para pruebas de hardware.
- Flujo de foto/edicion de imagen: desactivado por latencia y complejidad.
- Tool calling de Gemini para gestos: soportado, pero apagado por defecto para evitar gestos no deseados.

La referencia completa de funcionalidades esta en [docs/firmware.md](docs/firmware.md).

## Estructura importante

- [src/main.cpp](src/main.cpp): firmware completo.
- [data/gsec_stackchan_config_seed.csv](data/gsec_stackchan_config_seed.csv): CSV semilla para publicar en Google Sheets.
- [data/sfx/](data/sfx): audio PCM 24 kHz mono para LittleFS.
- [config/secrets.example.json](config/secrets.example.json): plantilla local sin secretos reales.
- [config/README.md](config/README.md): preparacion de credenciales y Wi-Fi.
- [docs/sfx_soundbank.md](docs/sfx_soundbank.md): catalogo de sonidos y triggers.
- [docs/stackchan-research.md](docs/stackchan-research.md): notas historicas de investigacion de hardware/comunidad.

## Secretos locales

No se deben commitear credenciales reales.

Crea `config/secrets.local.json` a partir de `config/secrets.example.json` y genera el archivo que se sube al robot:

```bash
python3 tools/prepare_device_fs.py
```

Archivos ignorados:

- `config/secrets.local.json`
- `data/secrets.json`

Validacion local sin imprimir secretos:

```bash
python3 tools/validate_local_config.py
```

Validacion con conexion real a Gemini Live:

```bash
python3 tools/validate_local_config.py --live
```

## Build y subida

```bash
python3 -m venv .venv
.venv/bin/pip install platformio
.venv/bin/pio run
```

Subir firmware:

```bash
.venv/bin/pio run -t upload
```

Subir LittleFS, incluyendo `secrets.json` y `data/sfx/*.pcm`:

```bash
.venv/bin/pio run -t uploadfs
```

Monitor serie:

```bash
.venv/bin/pio device monitor -p /dev/ttyACM0 -b 115200
```

## Operacion

- Arranque: carga LittleFS, secretos, Wi-Fi, SNTP Europe/Madrid y CSV remoto; si falla el CSV usa cache local.
- Hablar: tocar el sensor superior para iniciar, hablar, tocar de nuevo para cerrar la pregunta.
- Recargar CSV: doble toque en el cuarto superior de la pantalla.
- Bailar: doble toque en la zona inferior central.
- Tocar ojos/mejillas/boca en pantalla: reacciones locales con cara, servos y SFX.
- Agitar/inclinar: reacciones IMU locales.

## CSV

El firmware espera una hoja publicada como CSV con columnas:

```csv
section,key,value,enabled,order
```

Reglas practicas:

- No usar celdas multilinea.
- Usar comillas si el valor contiene comas.
- Dejar `enabled=yes` solo en filas activas.
- Ordenar con saltos de 10 para poder insertar filas.
- Mantener el prompt corto; el firmware trunca a 12000 bytes.

El CSV semilla actual esta en [data/gsec_stackchan_config_seed.csv](data/gsec_stackchan_config_seed.csv).
