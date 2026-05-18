# Mika Firmware Reference

Ultima revision: 2026-05-18.

Esta es la referencia operativa del firmware actual de Mika. Las notas de investigacion historicas estan en `docs/stackchan-research.md`.

## Hardware objetivo

- M5Stack StackChan K151 con CoreS3/ESP32-S3.
- Pantalla 320 x 240 ILI9342C con touch FT6336U.
- Sensor tactil superior de 3 zonas.
- Dos microfonos ES7210 y altavoz con amplificador I2S AW88298.
- Servos de pan/tilt con feedback.
- LEDs RGB WS2812C.
- IMU BMI270/BMM150.
- Camara GC0308, NFC ST25R3916 y sensor LTR553 presentes, pero no todos estan activos como funcionalidad de producto.

## Arranque

1. Inicializa StackChan BSP, servos, cara grafica e IMU.
2. Monta LittleFS.
3. Lee `/secrets.json`.
4. Conecta Wi-Fi con lista priorizada.
5. Sincroniza reloj por SNTP con zona `Europe/Madrid`.
6. Descarga el CSV remoto de configuracion.
7. Si el CSV es valido, lo guarda en `/cache_config.csv`; si falla, usa la cache.
8. Construye `RuntimeConfig` y `systemPrompt`.
9. Entra en modo idle: LED verde, cara neutral y listo para toque superior.

## Secretos y Wi-Fi

El archivo real del robot es `/secrets.json`, generado desde `config/secrets.local.json` con `tools/prepare_device_fs.py`.

Campos activos:

- `gemini_api_key`: API key de Gemini.
- `config_csv_url`: URL CSV publicada desde Google Sheets.
- `wifi_networks`: lista priorizada de redes con `ssid`, `password`, `auth` y `priority`.

El firmware escanea primero redes visibles y evita perder tiempo intentando SSID configuradas que no aparecen. Para redes abiertas usa `WiFi.begin(ssid)`, no password vacio.

Prioridad recomendada:

1. `GoogleGuest`, abierta.
2. `Google-Guest`, abierta.
3. Wi-Fi de desarrollo WPA.

## CSV remoto

Columnas esperadas:

```csv
section,key,value,enabled,order
```

Filas activas:

- `settings`: parametros de runtime.
- `prompt`: instrucciones concatenadas al prompt de sistema.
- `faq`: respuestas aprobadas que se insertan como FAQ en el prompt.
- `meta`: metadatos para humanos; no cambia comportamiento salvo que se use en tooling externo.

El firmware ignora filas `enabled` distintas de `yes`/`true`.

### Settings activos

| Key | Estado | Uso actual |
| --- | --- | --- |
| `model` | activo | Modelo Gemini Live. Valor actual recomendado: `gemini-3.1-flash-live-preview`. |
| `voice_name` | activo | Voz prebuilt de Gemini Live. Valor actual: `Puck`. |
| `response_timeout_seconds` | activo | Espera tecnica maxima de respuesta. Valor actual: `90`. |
| `hot_session_enabled` | activo | Reutiliza WebSocket Live entre turnos. |
| `hot_session_idle_timeout_seconds` | activo | Cierra sesion caliente tras inactividad. Valor actual: `90`. |
| `hot_session_max_age_seconds` | activo | Vida maxima de sesion caliente. Valor actual: `600`. |
| `hot_session_max_turns` | activo | Maximo de turnos antes de renovar sesion. Valor actual: `6`. |
| `conversation_memory_enabled` | activo | Mantiene resumen local de ultimos turnos. |
| `conversation_timeout_seconds` | activo | Caduca memoria conversacional. Valor actual: `180`. |
| `conversation_max_turns` | activo | Numero maximo de turnos recordados. Valor actual: `6`. |
| `playback_mode` | activo | `i2s` es el modo real recomendado. `buffered` se remapea a `i2s`. |
| `thinking_sfx_enabled` | activo | Permite rellenos de pensamiento aleatorios durante latencia. |
| `thinking_sfx_chance_percent` | activo | Probabilidad maxima de relleno. Se limita a 25. |
| `thinking_sfx_delay_ms` | activo | Delay minimo antes del relleno. Se limita a minimo 1300 ms. |
| `thinking_sfx_min_interval_seconds` | activo | Cooldown entre rellenos. Minimo 12 s. |
| `gemini_robot_tools_enabled` | activo | Expone tools a Gemini. Recomendado `false`. |
| `imu_reactions_enabled` | activo | Habilita reacciones por agitacion e inclinacion. |
| `vision_description_model` | dormido | Solo usado por flujo de foto, actualmente desactivado. |
| `image_edit_model` | dormido | Solo usado por flujo de foto, actualmente desactivado. |
| `image_edit_enabled` | forzado off | El parser fuerza `false` aunque el CSV diga `true`. |

Rows parseadas o mantenidas como intencion/compatibilidad pero que no gobiernan logica actual directa:

- `use_google_search`: se parsea, pero el setup Live actual no anade todavia `googleSearch`.
- `max_answer_seconds`: se parsea, pero la duracion real se controla por prompt y por `response_timeout_seconds`; no hay corte duro a 35 s.
- `enable_robot_tools`: se parsea y se loguea, pero no apaga los gestos locales.
- `language`: la voz/idioma dependen del prompt y de Gemini Live.
- `idle_timeout_seconds`, `max_prompt_bytes`, `start_trigger`, `audio_mode`: rows de documentacion/compatibilidad; el firmware usa constantes internas o el flujo `tap-to-stop`.

## Gemini Live

El firmware abre WebSocket seguro contra:

```text
generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent
```

El setup Live incluye:

- `responseModalities: ["AUDIO"]`.
- `systemInstruction` construido desde CSV y memoria conversacional.
- `speechConfig.voiceConfig.prebuiltVoiceConfig.voiceName` si `voice_name` no esta vacio.
- `inputAudioTranscription` y `outputAudioTranscription`.
- `automaticActivityDetection.disabled=true`, porque el firmware controla `activityStart`/`activityEnd`.
- Google Search no se anade actualmente al setup, aunque exista la row `use_google_search` en el CSV.
- Function calling para gestos solo si `gemini_robot_tools_enabled=true`.

La API key va en query parameter, dentro del robot. Esto es aceptado para el entorno fisico controlado de este proyecto.

## Sesion caliente

Con `hot_session_enabled=true`, Mika mantiene una sesion Live abierta entre preguntas para reducir latencia.

Se reutiliza mientras:

- Wi-Fi sigue conectado.
- El WebSocket sigue vivo.
- La configuracion efectiva no ha cambiado.
- No se supera `hot_session_idle_timeout_seconds`.
- No se supera `hot_session_max_age_seconds`.
- No se supera `hot_session_max_turns`.

La sesion se cierra tambien al recargar CSV, en errores de transporte, si un turno queda incompleto o ante frases claras de cierre/nuevo comienzo detectadas en el input.

## Conversacion

El firmware guarda localmente los ultimos turnos con transcripcion de usuario y respuesta de Mika.

- Timeout actual: 180 s.
- Maximo actual: 6 turnos.
- Se inyecta una version compacta en el prompt de nuevas sesiones Live.
- Hay frases de reset como "empecemos de nuevo" u "olvida lo anterior".

Gemini Live tambien mantiene contexto dentro de la sesion caliente; la memoria local cubre renovaciones de WebSocket.

## Modo de escucha

El modo final robusto es `tap-to-stop`:

1. Tocar sensor superior para iniciar turno.
2. Mika pasa a naranja y empieza a capturar micro.
3. El audio se envia a Gemini Live mientras se graba.
4. Al levantar el dedo, se arma el stop.
5. Tocar de nuevo para terminar la pregunta.
6. El firmware manda una cola adaptativa corta para no cortar la ultima silaba.
7. Envia `activityEnd` y espera audio de respuesta.

Constantes clave:

- Entrada: PCM mono 16 kHz, signed 16-bit.
- Chunk de micro streaming: 640 muestras, 40 ms.
- Minimo de grabacion: 700 ms.
- Maximo de grabacion: 12 s.
- Timeout sin voz: 3500 ms.
- Fin por silencio fallback: 900 ms.
- Stop tap hold: 140 ms.
- Cola adaptativa tras stop: 180, 260 o 420 ms segun silencio/voz reciente.

El modo antiguo de mantener pulsado hasta soltar queda como fallback de codigo, pero no es el flujo de producto actual.

## Latencia

Optimizaciones activas:

- Captura de micro en tarea separada desde el arranque del turno.
- Conexion Live preparada en paralelo mientras entra audio.
- Envio de audio streaming antes de `activityEnd`.
- Sesion Live caliente para evitar handshake/setup en cada turno.
- Prebuffer dinamico que arranca la reproduccion segun velocidad real de llegada de audio.
- SFX de pensamiento aleatorios y con cooldown para suavizar esperas sin tapar respuestas rapidas.

Metricas que aparecen por serie:

- `Mic stream ms/reason`.
- `Mic capture total_ms`.
- `Mic stream send_ms`.
- `Mic stop tail ms`.
- `Latency metrics stop_to_activityEnd_ms ... stop_to_playback_ms`.
- `I2S metrics prebuffer_ms ... response_underruns ... rx_pm ... max_gap_ms`.

## Audio de salida

Modo recomendado y actual: `i2s`.

- Salida Gemini: PCM 24 kHz, signed 16-bit.
- El firmware duplica mono a L/R.
- I2S por `I2S_NUM_1`.
- Amplificador AW88298.
- Pines actuales: BCLK 34, WS 33, DOUT 13.
- Volumen: maximo firmware (`255`).
- Ring buffer: 8 s de PCM stereo.
- Writes I2S: 20 ms.

Prebuffer:

- Base minima/adaptativa: 2 s.
- Decision dinamica: entre 900 y 3000 ms segun ratio de recepcion y gaps.
- Si hay underruns en respuestas largas, sube el prebuffer adaptativo.
- Si hay varios turnos limpios, lo baja gradualmente.

## SFX locales

Los SFX son ficheros PCM 24 kHz mono signed 16-bit little-endian bajo `/sfx/*.pcm` en LittleFS.

Usos principales:

- Rellenos de pensamiento.
- Reacciones tactiles.
- Chiquito de la Calzada.
- Cumplidos.
- Paloma.
- IMU shake/tilt.
- Baile con musica local.

El catalogo completo esta en `docs/sfx_soundbank.md`.

## Cara y expresiones

La cara es propia, dibujada con `M5Canvas`, inspirada en el estilo minimalista de StackChan/M5Stack-Avatar pero sin dependencia pesada.

Expresiones internas:

- `Neutral`
- `Happy`
- `Sleepy`
- `Doubt`
- `Surprised`
- `Sad`
- `Angry`
- `Dizzy`

Capas de vida:

- Parpadeo con jitter.
- Dobles parpadeos ocasionales.
- Microsacadas y sacadas oculares.
- Respiracion vertical suave.
- Lip-sync por RMS/apertura de boca, no por fonemas.
- Overlay de sonrojo para Paloma/cumplidos.

Estados visuales globales:

- Idle: LED verde.
- Listening: LED naranja.
- Thinking: LED azul/celeste.
- Speaking: cara con lip-sync.
- Error: LED rojo.

## Touch de pantalla

La pantalla tactil FT6336U se usa por zonas:

- Ojo izquierdo/derecho: queja local, cara reactiva, servo lateral y SFX `ouch`.
- Mejilla izquierda/derecha: gesto amable, inclinacion, sonrisa y `happy_mmm`.
- Boca en idle: cara de asombro, gesto corto y SFX `neutral_surprise_ooh` o `neutral_surprise_gasp`.
- Cuarto superior con doble toque: recarga CSV remoto, reinicia memoria y cierra sesion Live caliente.
- Zona inferior central con doble toque: baile con musica local.

El sensor tactil superior fisico se reserva para voz y no depende de la pantalla.

## Servos y gestos

Gestos locales activos:

- Listening tilt al iniciar escucha.
- Thinking pose al esperar.
- Speaking pose al responder.
- Shake de negacion ante rechazos o contenido peligroso.
- Nod de afirmacion cuando se dispara explicitamente.
- Surprised pose al tocar boca.
- Cheek lean al tocar mejilla.
- Dizzy wobble al agitar.
- Tilt complaint al inclinar.
- Dance 1 con musica.
- Dance 2 y 3 existen como gestos de shake/nod, pero no deben dispararse en saludos simples.

Los gestos locales estan activos en el firmware actual. La row `enable_robot_tools` solo se parsea/loguea; no es un interruptor efectivo. `gemini_robot_tools_enabled=false` evita que Gemini decida gestos via tool calling.

## Huevos de pascua y triggers de lenguaje

### Paloma

Si el input/output menciona "Paloma":

- Sonrojo con dos puntos pequenos por mejilla.
- Sonrisa.
- Inclinacion de cabeza.
- Sin corazon.
- Cooldown aproximado: 10 s.

### Cumplidos

Triggers como "Mika eres guapa", "que bonita", "me caes bien" o "te quiero Mika":

- Sonrojo/timidez.
- Risa suave.
- Cooldown aproximado: 12 s.

### Chiquito

Triggers como "Chiquito", "Calzada", "fistro", "pecador", "jarl", "no puedor", "torpedo", "grijander", "condemor":

- Se ejecuta antes de la respuesta, no al final.
- Movimiento exagerado.
- Seleccion aleatoria sin repeticion inmediata entre clips disponibles.
- Luego Mika se centra y contesta normal.
- Cooldown aproximado: 24 s.

### Peticiones peligrosas

Si el input parece pedir hackeo ofensivo, malware, robo de cuentas o saltarse controles:

- Cara triste.
- SFX triste local.
- Gesto de negacion.
- Prompt redirige hacia defensa y aprendizaje seguro.

## IMU

Activado por `imu_reactions_enabled=true`.

Shake:

- Detecta delta de aceleracion sobre 0.48 g o giro sobre 430 dps.
- Cara mareada.
- Movimiento de mareo.
- Clip aleatorio `dizzy_*` sin repeticion inmediata.
- Cooldown: 2600 ms.

Tilt:

- Se dispara alrededor de 22 grados.
- Reset al volver por debajo de 12 grados.
- Debe mantenerse unos 260 ms.
- Cara de sorpresa/queja.
- Clip aleatorio `tilt_*` sin repeticion inmediata.

La IMU recalibra baseline al arrancar y lo adapta cuando el robot esta quieto.

## Baile

El baile principal se lanza con doble toque en la zona inferior central.

- Usa `dance_loop_24k.pcm`.
- Mueve servos en coreografia local.
- Mantiene cara feliz un poco despues de terminar para no cortar la expresion de golpe.

`dance_2` y `dance_3` estan implementados como gestos cortos de shake/nod mas que como bailes completos.

## Funcionalidades desactivadas

### Presencia ambiental LTR553

El servicio existe, pero `kAmbientPresenceEnabled=false`.

Motivo: producia sonidos idle espontaneos que molestaban en demo. Actualmente Mika no deberia emitir sonidos por proximidad/cambios de luz.

### NFC

El codigo de diagnostico NFC existe, pero `kNfcDiagnosticsEnabled=false`.

Si se activa, lee UID/NDEF y lo vuelca por serie. No hay experiencia de usuario asociada.

### Camara e imagen

El flujo de foto existe en codigo, pero `imageEditEnabled` se fuerza a `false` aunque el CSV lo active.

Motivo: la captura/descripcion/edicion con modelos de imagen tuvo demasiada latencia y errores para la experiencia principal.

### Barge-in tactil durante respuesta

El codigo existe, pero `kTouchBargeInEnabled=false`.

Motivo: produjo bucles de interrupcion y reinicio de frase. Para cortar una respuesta se puede usar el touch de boca si el flujo lo permite, pero no es la interaccion principal validada.

## Herramientas locales

- `tools/prepare_device_fs.py`: genera `data/secrets.json` desde secretos locales.
- `tools/validate_local_config.py`: valida secretos, CSV y opcionalmente Gemini Live.
- `tools/generate_sfx_live.py`: genera SFX no linguisticos con Live API.
- `tools/generate_tts_sfx.py`: genera frases expresivas con Gemini TTS/Puck y anade silencio final para evitar cortes.

## Checklist de demo

1. `python3 tools/prepare_device_fs.py`
2. `.venv/bin/pio run`
3. `.venv/bin/pio run -t uploadfs`
4. `.venv/bin/pio run -t upload`
5. Monitor serie y verificar:
   - `OK LittleFS`
   - `OK WiFi`
   - `OK config downloaded` o cache valida
   - `Voice: Puck`
   - `Playback: i2s`
   - `Ambient presence: off`
   - `Touch top for voice turn`
6. Prueba voz: tocar arriba, preguntar, tocar arriba de nuevo.
7. Prueba CSV: doble toque superior y confirmar recarga.
8. Prueba tactil: ojos, mejillas, boca.
9. Prueba IMU: agitar suave e inclinar.
10. Prueba baile: doble toque inferior central.
