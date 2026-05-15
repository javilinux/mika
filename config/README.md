# Configuracion local

Rellena `config/secrets.local.json` en esta maquina. No pegues esos valores en el chat.

Campos:

- `gemini_api_key`: API key de Gemini.
- `wifi_ssid`: nombre de la red Wi-Fi 2.4 GHz.
- `wifi_password`: password de la red Wi-Fi.
- `wifi_networks`: lista priorizada de redes que el firmware debe probar.
- `config_csv_url`: URL publicada del Google Sheet en formato CSV.

El archivo `config/secrets.local.json` esta ignorado por `.gitignore`.

Para produccion, el firmware debe probar primero estas redes abiertas:

1. `GoogleGuest`
2. `Google-Guest`

Y despues caer a la Wi-Fi de desarrollo configurada en `wifi_ssid`/`wifi_password`.

En Arduino, una red abierta debe conectarse con `WiFi.begin(ssid)` y no con password vacio. Esto evita depender del comportamiento de la app oficial de StackChan.

Para validar la configuracion local sin imprimir secretos:

```bash
python3 tools/validate_local_config.py
```

Para validar tambien una sesion Gemini Live WebSocket real:

```bash
python3 tools/validate_local_config.py --live
```
