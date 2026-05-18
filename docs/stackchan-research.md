# StackChan Research Notes

Fecha de investigacion: 2026-05-15

Nota: este documento conserva investigacion, pruebas y decisiones historicas. La referencia actual del firmware implementado esta en `docs/firmware.md`. Algunas secciones antiguas de pruebas pueden mencionar valores o modos que ya han cambiado.

## Identificacion local

El robot conectado por USB aparece en Linux como:

- USB: `303a:1001 Espressif USB JTAG/serial debug unit`
- Puerto serie: `/dev/ttyACM0`
- Enlace estable: `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_1C:DB:D4:BA:4F:70-if00`
- Driver kernel: `cdc_acm`
- Interfaces USB: CDC serial y una interfaz vendor-specific de JTAG/debug Espressif.

Conclusion practica: el host interno es un ESP32-S3 con USB nativo. Esto encaja con el StackChan oficial de M5Stack basado en CoreS3, no con las variantes comunitarias antiguas basadas en M5Stack Basic/Core2.

## Modelo confirmado

Modelo: M5Stack StackChan, SKU `K151`.

Confirmado por la caja fisica del dispositivo. La variante `K151-R` es el kit que incluye mando remoto; esta unidad se identifica como `K151`.

Arquitectura:

- Controlador principal: M5Stack CoreS3.
- SoC: ESP32-S3, doble nucleo Xtensa LX7 a 240 MHz.
- Memoria: 16 MB Flash, 8 MB PSRAM.
- Comunicacion: Wi-Fi 2.4 GHz 802.11 b/g/n, Bluetooth 5 LE, USB CDC, USB OTG.
- Desarrollo soportado oficialmente: Arduino IDE, UiFlow2, PlatformIO, ESP-IDF.

## Hardware relevante

Modulo host CoreS3:

- Pantalla IPS 2.0 in, 320 x 240, driver ILI9342C.
- Touch capacitivo, driver FT6336U.
- Camara GC0308, 640 x 480, 0.3 MP.
- Sensor proximidad/luz LTR-553ALS-WA.
- IMU 9 ejes: BMI270 + BMM150.
- microSD.
- Altavoz 1 W con amplificador I2S AW88298.
- Dos microfonos con codec ES7210.
- PMIC AXP2101.
- RTC BM8563.
- Expansor IO AW9523B.

Modulo cuerpo/base:

- Bateria 550 mAh.
- Dos servos con feedback:
  - eje X horizontal: rotacion continua 360 grados.
  - eje Y vertical: movimiento 90 grados.
- 12 LEDs RGB WS2812C, dos filas de 6.
- IR emisor y receptor.
- Touch superior de 3 zonas, controlador Si12T.
- NFC ST25R3916.
- Monitor bateria/potencia INA226.
- Expansor IO PY32L020.
- Tres puertos Grove/HY2.0-4P.

Dimensiones y peso oficiales:

- 54.0 x 70.5 x 61.5 mm.
- 187.2 g.

## Especificaciones impresas en la caja

La caja del StackChan K151 lista estas caracteristicas:

- Main Controller:
  - ESP32-S3 MCU.
  - Dual-core LX7 a 240 MHz.
  - 16 MB Flash + 8 MB PSRAM.
- Motors:
  - servo pan 360 grados con feedback.
  - servo tilt 90 grados con feedback.
- Wireless:
  - Wi-Fi 2.4 GHz 802.11 b/g/n.
  - Bluetooth 5 LE.
  - IR Rx/Tx IRM56384.
- Wired:
  - USB-C CDC y USB OTG full-speed.
  - GPIO, UART, I2C.
- Sensors:
  - dual microphones + ES7210 codec.
  - proximity sensor LTR-553ALS-WA.
  - 9-axis IMU BMI270 + BMM150.
  - 3-zone touch Si12T.
  - NFC ST25R3916.
- Display:
  - 2.0 inch IPS LCD, 320 x 240, ILI9342C.
  - capacitive multi-touch FT6336U.
- Interaction:
  - 1 W speaker + AW88298 I2S amplifier.
  - 12 RGB LEDs WS2812C.
  - power LED.
  - power and reset buttons.
- Camera:
  - GC0308, 640 x 480, 0.3 MP.
- Expansion:
  - microSD slot.
  - Grove x3.
  - LEGO compatible mounting.
- Power:
  - 550 mAh battery.
  - PMIC AXP2101 + RTC BM8563.
  - USB-C power and data.

## Pines e interfaces

PinMap del cuerpo StackChan:

| Funcion | GPIO |
| --- | --- |
| Servo_TX | GPIO6 |
| Servo_RX | GPIO7 |
| IR_SEND | GPIO5 |
| IR_REC | GPIO10 |
| I2C_SCL | GPIO11 |
| I2C_SDA | GPIO12 |

I2C del cuerpo:

| Dispositivo | Direccion |
| --- | --- |
| INA226 bateria/potencia | `0x41` |
| ST25R3916 NFC | `0x50` |
| Si12T touch superior | `0x68` |
| PY32L020 IO expander | `0x6F` por defecto, `0x71` configurable |

Puertos Grove/HY2.0-4P:

| Puerto | Negro | Rojo | Amarillo | Blanco |
| --- | --- | --- | --- | --- |
| PORT.A | GND | 5V | GPIO2 | GPIO1 |
| PORT.B | GND | 5V | GPIO9 | GPIO8 |
| PORT.C | GND | 5V | GPIO17 | GPIO18 |

Pines utiles desde la integracion ESPHome/Home Assistant:

- Audio I2S: LRCLK GPIO33, BCLK GPIO34, MCLK GPIO0.
- Microfono I2S DIN: GPIO14.
- Altavoz I2S DOUT: GPIO13.
- I2C principal: SDA GPIO12, SCL GPIO11.
- SPI pantalla: CLK GPIO36, MOSI GPIO37, DC GPIO35, CS GPIO3.
- UART servos: TX GPIO6, RX GPIO7, 1 Mbps.
- Camara GC0308: VSYNC GPIO46, HREF GPIO38, XCLK GPIO2, PCLK GPIO45, D0-D7 GPIO39/40/41/42/15/16/48/47.

## Software y firmware

### Firmware de fabrica

Repositorio oficial: `m5stack/StackChan`.

Incluye:

- firmware del dispositivo.
- firmware del mando remoto.
- app movil iOS/Android.
- servidor.

La documentacion avisa que el repositorio open-source puede ir algo por detras del firmware y de las apps publicadas.

Funciones de fabrica relevantes:

- AI Agent con voz y animaciones.
- App StackChan World para binding, configuracion, video remoto, avatar, motion y dance.
- ESP-NOW remote control.
- OTA/update desde el propio dispositivo.
- App Center.
- Restauracion oficial con M5Burner.

### Arduino

Ruta oficial para prototipos de bajo nivel:

- Board Manager: seleccionar `M5CoreS3`.
- M5Stack Board Manager version minima documentada: `>= 3.2.2`.
- Libreria Arduino: `M5StackChan` / repositorio `m5stack/StackChan-BSP`.
- Version reciente verificada por Git/GitHub: tag `1.1.0`, publicado como latest el 2026-05-12.
- Dependencias del BSP:
  - `M5Unified`
  - `IRremoteESP8266`
  - `M5Unit-NFC`

API Arduino vista en ejemplos oficiales:

- `M5StackChan.begin()`
- `M5StackChan.update()`
- `M5StackChan.Display()`
- `M5StackChan.Motion.goHome()`
- `M5StackChan.Motion.move(x, y)`
- `M5StackChan.Motion.moveX(angle, speed)`
- `M5StackChan.Motion.moveY(angle, speed)`
- `M5StackChan.Motion.rotateX(velocity)`
- `M5StackChan.Motion.stop()`
- `M5StackChan.TouchSensor.wasPressed()`
- `M5StackChan.setRgbColor(index, r, g, b)`
- `M5StackChan.refreshRgb()`

Notas de servos en Arduino:

- La unidad de angulo usada por la libreria es `10 = 1 grado`.
- Rango de ejemplo: X `-1280` a `1280` (-128 a 128 grados), Y `0` a `900` (0 a 90 grados).
- Solo X permite rotacion continua.
- Velocidad X continua: `-1000` a `1000`.

### UiFlow2

Ruta oficial de bajo codigo/grafica. Es util para pruebas rapidas, pero para un proyecto controlado desde este repo probablemente Arduino/PlatformIO o ESP-IDF nos daran mas control y versionado.

### ESPHome / Home Assistant

Hay guia oficial para integrar StackChan con Home Assistant:

- Requiere Home Assistant con ESPHome Builder.
- Crear dispositivo como `ESP32-S3` y seleccionar `M5Stack CoreS3`.
- Paquete remoto:

```yaml
packages:
  remote_package_files:
    url: https://github.com/m5stack/esphome-yaml
    files: [examples/kit/stackchan-bsp.factory.yaml]
    ref: main
    refresh: 0s
```

Limite importante: la firmware de voice assistant para HA no integra todos los perifericos por rendimiento. La guia indica que incluye voz, display, touch y control de servos basico.

### Gemini Live API

Prueba local validada el 2026-05-15:

- La API key configurada permite listar modelos.
- El modelo `models/gemini-3.1-flash-live-preview` aparece disponible.
- El WebSocket real abre contra:
  - `wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=...`
- El formato de primer mensaje que funciono en la prueba practica fue:
  - `setup.model`
  - `setup.generationConfig.responseModalities = ["AUDIO"]`
  - `setup.generationConfig.speechConfig.voiceConfig.prebuiltVoiceConfig.voiceName` cuando se configura `settings,voice_name`
  - `setup.systemInstruction`
  - `setup.outputAudioTranscription = {}`
- Una prueba con texto recibio chunks de audio y transcripcion de salida.

Nota importante: aunque la pagina raw WebSocket actual muestra un ejemplo con clave superior `config`, en la prueba real de este entorno el servidor cerro la conexion con ese formato. El firmware debe implementar inicialmente la variante comprobada `setup` + `generationConfig` y mantener esta capa aislada para poder adaptarla si Google cambia el protocolo.

Prueba inicial en StackChan K151 validada el 2026-05-15:

Estas notas describen el primer camino que funciono. El firmware actual ya no usa grabacion fija de 5 s ni `M5.Speaker.playRaw()` para las respuestas principales; la referencia vigente esta en `docs/firmware.md`.

- Firmware PlatformIO/Arduino flasheado correctamente en `/dev/ttyACM0`.
- LittleFS subido con `/secrets.json`.
- El robot no ve `GoogleGuest` ni `Google-Guest` en el entorno de desarrollo actual.
- En desarrollo se uso una red Wi-Fi local de 2.4 GHz como fallback.
- Wi-Fi conectado correctamente.
- SNTP correcto.
- Google Sheet descargado por HTTPS:
  - HTTP 200.
  - CSV de 4457 bytes.
  - 33 filas parseadas.
- Prompt construido:
  - modelo `gemini-3.1-flash-live-preview`.
  - 3594 bytes de system prompt.
- Gemini Live:
  - TLS + WebSocket upgrade correcto.
  - `setupComplete` recibido.
  - respuesta de audio recibida en frames WebSocket binarios con JSON textual.
  - 12 chunks `inlineData.data` detectados en la prueba final.
- Audio de salida probado inicialmente:
  - `inlineData.data` se extrae del JSON textual recibido en frames binarios.
  - base64 decodificado en firmware con `mbedtls_base64_decode`.
  - PCM 16-bit little-endian convertido a `int16_t`.
  - reproduccion por `M5.Speaker.playRaw(..., 24000, mono)`.
  - prueba posterior: 10 chunks de audio, 33841 muestras PCM, `OK audio queued`, `Audio playback done`.
- Turno de voz por touch probado inicialmente:
  - toque superior detectado con `M5StackChan.TouchSensor`.
  - microfono grabado durante 5 s a 16 kHz mono.
  - microfono y altavoz no se usan simultaneamente: `Speaker.end()` antes de `Mic.begin()` y vuelta a `Speaker.begin()` antes de reproducir.
  - Live configurado con `realtimeInputConfig.automaticActivityDetection.disabled = true`.
  - el firmware manda `activityStart`, chunks `realtimeInput.audio` de 20 ms y `activityEnd`.
  - `settings,playback_mode` evoluciono durante las pruebas:
    - `buffered`: historico; esperaba a recibir el audio completo y lo reproducia con `playRaw`.
    - `streaming`: historico/experimental; hacia prebuffer y encadenaba `playRaw`, pero se entrecortaba.
    - `i2s`: modo vigente recomendado. Usa el driver I2S STD directo sobre CoreS3 (`I2S_NUM_1`, BCLK GPIO34, WS GPIO33, DOUT GPIO13), una tarea FreeRTOS y ring buffer PCM; evita encadenar `M5.Speaker.playRaw()`.
  - `settings,max_answer_seconds` queda como objetivo de brevedad del prompt, no como corte duro. El corte tecnico separado es `settings,response_timeout_seconds`, por defecto 90 s.
  - se fuerza pronunciacion de GSEC en el prompt fijo: `GSEC` debe decirse como `yisec`, sonando `yi-sec`.
  - prueba real: entrada transcrita como `¿Qué es un virus?`, 71 chunks de audio, 322082 muestras PCM, `Audio playback done`, `OK Live voice turn`.
  - prueba adicional: respuesta de bienvenida, 83 chunks de audio, 372481 muestras PCM, `OK Live voice turn`.

Notas de implementacion derivadas:

- `ArduinoWebsockets@0.5.4` no resulto adecuado para este entorno porque en ESP32 su `setInsecure()` no llama a `WiFiClientSecure::setInsecure()`. El diagnostico usa un cliente WebSocket minimo propio sobre `WiFiClientSecure`.
- Las respuestas Live llegan como frames WebSocket opcode `2` aunque el payload sea JSON textual. El parser debe aceptar texto tanto en opcode `1` como en opcode `2`.
- No conviene parsear los frames grandes de audio completos con ArduinoJson; el firmware actual extrae `inlineData.data` con busqueda estructurada ligera y deja ArduinoJson para mensajes pequenos/configuracion.
- El primer intento con VAD automatico y `audioStreamEnd` no genero respuesta fiable: el servidor solo devolvio `sessionResumptionUpdate`. Para este modo pulsar-hablar-fijar-5s, el cierre manual `activityStart`/`activityEnd` es mas determinista.

## Modo descarga, permisos y restauracion

Modo descarga:

- Conectar por USB-C. M5Stack recomienda usar el USB-C de la base para reducir riesgo por movimiento de motores.
- Mantener pulsado `RST` unos 3 segundos.
- Soltar cuando el LED indicador se ponga verde.

Restaurar firmware oficial:

- Usar M5Burner.
- Buscar `StackChan`.
- Activar `Only Official`.
- Descargar y flashear el firmware oficial.

Permisos Linux:

- El dispositivo crea `/dev/ttyACM0` con grupo `dialout`.
- Se anadio el usuario `user` a `dialout`, pero requiere cerrar sesion y volver a entrar para aplicarse globalmente.
- Para esta sesion se aplico un ACL temporal sobre `/dev/ttyACM0`.

## Riesgos y restricciones

Servo Y:

- M5Stack recomienda controlar el eje Y vertical entre 5 y 85 grados.
- Usar extremos puede bloquear el servo y danarlo permanentemente.

Motores:

- No forzar manualmente partes moviles si hay resistencia.
- Si hay resistencia, el motor esta alimentado/controlado; forzarlo puede danar el hardware.

Magnetometro:

- Evitar imanes o campos magneticos fuertes cerca si vamos a usar orientacion basada en BMM150.

Wi-Fi:

- La configuracion de fabrica usa Wi-Fi 2.4 GHz; no soporta 5 GHz para el onboarding indicado por M5Stack.

Audio:

- En los ejemplos M5Unified, microfono y altavoz no se usan simultaneamente: se apaga uno antes de activar el otro.
- En ESPHome, el voice assistant usa audio a 16 kHz para microfono y pipeline de salida a 48 kHz segun ejemplo oficial.

## Recomendacion inicial para el proyecto

Para empezar sin perder funciones de fabrica, la ruta mas prudente era:

1. No flashear todavia.
2. Leer logs serie y confirmar firmware/version si expone salida.
3. Decidir si queremos:
   - extender firmware de fabrica,
   - construir firmware Arduino/PlatformIO propio,
   - usar ESPHome/Home Assistant,
   - o usar el robot como periferico controlado por serial/Wi-Fi desde un agente externo.
4. Si vamos a flashear, primero documentar procedimiento de backup/restauracion con M5Burner.

El proyecto actual se ha decidido como robot-only, por lo que estas opciones quedan como alternativa de rescate. Para un proyecto AI controlado desde este PC, mi sesgo tecnico seria:

- PC/Raspberry/host hace STT/LLM/TTS pesado.
- StackChan ejecuta firmware ligero para display, servos, LEDs, audio I/O y sensores.
- Comunicacion por USB serial o Wi-Fi local.
- Evitar meter demasiada logica AI en el ESP32-S3, porque PSRAM y CPU son limitados para audio + vision + movimiento + red.

## Fuentes consultadas

- M5Stack StackChan product docs: https://docs.m5stack.com/en/stackchan
- M5Stack StackChan GitHub oficial: https://github.com/m5stack/StackChan
- M5Stack StackChan Arduino/BSP: https://github.com/m5stack/StackChan-BSP
- StackChan Arduino Quick Start: https://docs.m5stack.com/en/arduino/stackchan/program
- StackChan Arduino servo example: https://docs.m5stack.com/en/arduino/stackchan/servo
- StackChan Arduino RGB example: https://docs.m5stack.com/en/arduino/stackchan/rgb
- StackChan Arduino mic example: https://docs.m5stack.com/en/arduino/stackchan/mic
- StackChan Arduino camera example: https://docs.m5stack.com/en/arduino/stackchan/camera
- StackChan Home Assistant integration: https://docs.m5stack.com/en/homeassistant/kit/stackchan
- Community/original Stack-chan project: https://github.com/stack-chan/stack-chan
