# Mika Local SFX Soundbank

Todos los ficheros son PCM 24 kHz, mono, signed 16-bit little-endian, listos para reproducir por I2S desde LittleFS.

Los previews WAV se generan bajo `generated/sfx_preview/`. Los clips TTS expresivos se generan con `tools/generate_tts_sfx.py`, voz `Puck`, y llevan silencio final anadido para evitar cortes bruscos al terminar.

## Catalogo

| Clip | PCM file | Duration | Uso actual |
| --- | --- | ---: | --- |
| `ouch` | `data/sfx/ouch_24k.pcm` | 0.70 s | Toque en ojo |
| `happy_mmm` | `data/sfx/happy_mmm_24k.pcm` | 1.00 s | Toque de mejilla |
| `giggle` | `data/sfx/giggle_24k.pcm` | 0.47 s | Risa local heredada |
| `dance_loop` | `data/sfx/dance_loop_24k.pcm` | 9.19 s | Baile principal |
| `neutral_giggle_soft` | `data/sfx/neutral_giggle_soft_24k.pcm` | 0.45 s | Cumplido a Mika, risa suave |
| `neutral_giggle_bright` | `data/sfx/neutral_giggle_bright_24k.pcm` | 0.42 s | Risa mas marcada |
| `neutral_thinking_mmm` | `data/sfx/neutral_thinking_mmm_24k.pcm` | 1.00 s | Pensamiento/demora |
| `neutral_thinking_hmm` | `data/sfx/neutral_thinking_hmm_24k.pcm` | 0.25 s | Duda corta |
| `think_mmm_soft` | `data/sfx/think_mmm_soft_24k.pcm` | 0.65 s | Latency filler, pensamiento suave |
| `think_mmm_rise` | `data/sfx/think_mmm_rise_24k.pcm` | 0.58 s | Latency filler, pensamiento curioso |
| `think_mmm_short` | `data/sfx/think_mmm_short_24k.pcm` | 0.65 s | Latency filler corto |
| `think_hmm_soft` | `data/sfx/think_hmm_soft_24k.pcm` | 0.50 s | Latency filler, duda suave |
| `neutral_ack_aha` | `data/sfx/neutral_ack_aha_24k.pcm` | 0.41 s | Acknowledgement |
| `neutral_ack_mhm` | `data/sfx/neutral_ack_mhm_24k.pcm` | 0.80 s | Acknowledgement suave |
| `neutral_surprise_ooh` | `data/sfx/neutral_surprise_ooh_24k.pcm` | 0.77 s | Toque en boca, asombro |
| `neutral_surprise_gasp` | `data/sfx/neutral_surprise_gasp_24k.pcm` | 0.60 s | Toque en boca, sobresalto |
| `neutral_sad_aww` | `data/sfx/neutral_sad_aww_24k.pcm` | 0.43 s | Peticion peligrosa/rechazo |
| `neutral_relief_phew` | `data/sfx/neutral_relief_phew_24k.pcm` | 0.54 s | Recuperacion/error leve |
| `neutral_confused_huh` | `data/sfx/neutral_confused_huh_24k.pcm` | 0.57 s | Confusion |
| `neutral_tada` | `data/sfx/neutral_tada_24k.pcm` | 0.90 s | Recarga CSV correcta, celebracion |
| `chiquito_grito` | `data/sfx/chiquito_grito_24k.pcm` | 0.98 s | Huevo de pascua Chiquito |
| `chiquito_epetekan` | `data/sfx/chiquito_epetekan_24k.pcm` | 3.07 s | Huevo de pascua Chiquito |
| `dizzy_mareao` | `data/sfx/dizzy_mareao_24k.pcm` | 3.64 s | IMU shake / mareo |
| `dizzy_meneito` | `data/sfx/dizzy_meneito_24k.pcm` | 2.70 s | IMU shake / mareo |
| `dizzy_bits` | `data/sfx/dizzy_bits_24k.pcm` | 3.25 s | IMU shake / mareo |
| `dizzy_firewalls` | `data/sfx/dizzy_firewalls_24k.pcm` | 3.18 s | IMU shake / mareo |
| `tilt_cuidao` | `data/sfx/tilt_cuidao_24k.pcm` | 2.66 s | IMU tilt / queja |
| `tilt_derechito` | `data/sfx/tilt_derechito_24k.pcm` | 2.60 s | IMU tilt / queja |
| `tilt_wifi` | `data/sfx/tilt_wifi_24k.pcm` | 2.02 s | IMU tilt / queja |
| `tilt_desconfiguro` | `data/sfx/tilt_desconfiguro_24k.pcm` | 3.26 s | IMU tilt / queja |

## Triggers cableados

- CSV reload success: `neutral_tada`.
- Mouth touch while idle: `neutral_surprise_ooh` o `neutral_surprise_gasp`.
- Eye touch: `ouch`.
- Cheek touch: `happy_mmm`.
- Dangerous/refused request: `neutral_sad_aww`.
- Compliment to Mika: `neutral_giggle_soft`.
- Chiquito mention: `chiquito_grito` o `chiquito_epetekan`, sin repeticion inmediata.
- IMU shake: uno de los `dizzy_*`, sin repeticion inmediata.
- IMU tilt: uno de los `tilt_*`, sin repeticion inmediata.
- Thinking filler: uno de `think_mmm_soft`, `think_mmm_rise`, `think_mmm_short` o `think_hmm_soft`, con probabilidad y cooldown desde CSV.
- Dance: `dance_loop`.

## Triggers desactivados

- Presencia/proximidad idle: desactivada en firmware (`kAmbientPresenceEnabled=false`).
- Camara/foto: flujo de imagen dormido; no dispara SFX de producto.
- NFC: diagnostico dormido; no dispara SFX de producto.
