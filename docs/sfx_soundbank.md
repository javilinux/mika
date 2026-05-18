# Mika Neutral SFX Soundbank

All files are 24 kHz, mono, signed 16-bit little-endian PCM for firmware playback.
WAV previews are generated under `generated/sfx_preview/`.

| Clip | PCM file | Duration | Suggested use |
| --- | --- | ---: | --- |
| `neutral_giggle_soft` | `data/sfx/neutral_giggle_soft_24k.pcm` | 0.45 s | Small friendly laugh, cheek touch, playful idle reaction |
| `neutral_giggle_bright` | `data/sfx/neutral_giggle_bright_24k.pcm` | 0.42 s | Stronger laugh, joke, celebration |
| `neutral_thinking_mmm` | `data/sfx/neutral_thinking_mmm_24k.pcm` | 1.00 s | Thinking, search, delayed answer |
| `neutral_thinking_hmm` | `data/sfx/neutral_thinking_hmm_24k.pcm` | 0.25 s | Quick doubt or curiosity |
| `neutral_thinking_mmm_soft` | `data/sfx/think_mmm_soft_24k.pcm` | 0.65 s | Latency filler, soft thinking |
| `neutral_thinking_mmm_rise` | `data/sfx/think_mmm_rise_24k.pcm` | 0.58 s | Latency filler, curious thinking |
| `neutral_thinking_mmm_short` | `data/sfx/think_mmm_short_24k.pcm` | 0.65 s | Latency filler, quick thinking |
| `neutral_thinking_hmm_soft` | `data/sfx/think_hmm_soft_24k.pcm` | 0.50 s | Latency filler, soft doubt |
| `neutral_ack_aha` | `data/sfx/neutral_ack_aha_24k.pcm` | 0.41 s | Acknowledgement, understood |
| `neutral_ack_mhm` | `data/sfx/neutral_ack_mhm_24k.pcm` | 0.80 s | Soft acknowledgement |
| `neutral_surprise_ooh` | `data/sfx/neutral_surprise_ooh_24k.pcm` | 0.77 s | Amazed face, mouth touch |
| `neutral_surprise_gasp` | `data/sfx/neutral_surprise_gasp_24k.pcm` | 0.60 s | Sudden surprise |
| `neutral_sad_aww` | `data/sfx/neutral_sad_aww_24k.pcm` | 0.43 s | Rejection, blocked request, sad expression |
| `neutral_relief_phew` | `data/sfx/neutral_relief_phew_24k.pcm` | 0.54 s | Recovery after error, relief |
| `neutral_confused_huh` | `data/sfx/neutral_confused_huh_24k.pcm` | 0.57 s | Confusion, unclear touch/event |
| `neutral_tada` | `data/sfx/neutral_tada_24k.pcm` | 0.90 s | Celebration, dance start/end |
| `chiquito_grito` | `data/sfx/chiquito_grito_24k.pcm` | 0.98 s | Chiquito de la Calzada easter egg |
| `chiquito_epetekan` | `data/sfx/chiquito_epetekan_24k.pcm` | 3.07 s | Chiquito de la Calzada easter egg |
| `dizzy_mareao` | `data/sfx/dizzy_mareao_24k.pcm` | 2.30 s | IMU shake / dizzy reaction |
| `dizzy_meneito` | `data/sfx/dizzy_meneito_24k.pcm` | 2.25 s | IMU shake / dizzy reaction |
| `dizzy_bits` | `data/sfx/dizzy_bits_24k.pcm` | 2.40 s | IMU shake / dizzy reaction |
| `dizzy_firewalls` | `data/sfx/dizzy_firewalls_24k.pcm` | 2.51 s | IMU shake / dizzy reaction |
| `tilt_cuidao` | `data/sfx/tilt_cuidao_24k.pcm` | 1.73 s | IMU tilt complaint |
| `tilt_derechito` | `data/sfx/tilt_derechito_24k.pcm` | 1.38 s | IMU tilt complaint |
| `tilt_wifi` | `data/sfx/tilt_wifi_24k.pcm` | 1.82 s | IMU tilt complaint |
| `tilt_desconfiguro` | `data/sfx/tilt_desconfiguro_24k.pcm` | 2.02 s | IMU tilt complaint |

Current generation voice: `Puck`.

Current wired triggers:

- CSV reload success: `neutral_tada`
- Mouth touch while idle: `neutral_surprise_ooh` or `neutral_surprise_gasp`
- Dangerous/refused request: `neutral_sad_aww`
- Idle near/covered presence: disabled in firmware; it was too noisy in practice
- Compliment to Mika: `neutral_giggle_soft`
- Chiquito mention: `chiquito_grito` or `chiquito_epetekan`
- IMU shake: one of the `dizzy_*` clips, no immediate repeat
- IMU tilt: one of the `tilt_*` clips, no immediate repeat
