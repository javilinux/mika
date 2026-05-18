# Configuracion local

Rellena `config/secrets.local.json` en esta maquina. No pegues esos valores en el chat.

Campos:

- `gemini_api_key`: API key de Gemini.
- `wifi_ssid`: nombre de la red Wi-Fi 2.4 GHz de desarrollo, mantenido como fallback.
- `wifi_password`: password de la red Wi-Fi de desarrollo.
- `wifi_networks`: lista priorizada de redes que el firmware debe probar. Es el campo que usa el firmware actual.
- `config_csv_url`: URL publicada del Google Sheet en formato CSV.

El archivo `config/secrets.local.json` esta ignorado por `.gitignore`.

Genera el archivo real de LittleFS con:

```bash
python3 tools/prepare_device_fs.py
```

Ese comando crea `data/secrets.json`, tambien ignorado por git, y ordena `wifi_networks` por `priority`.

Para produccion, el firmware debe probar primero estas redes abiertas:

1. `GoogleGuest`
2. `Google-Guest`

Y despues caer a la Wi-Fi de desarrollo configurada en `wifi_ssid`/`wifi_password`.

En Arduino, una red abierta debe conectarse con `WiFi.begin(ssid)` y no con password vacio. Esto evita depender del comportamiento de la app oficial de StackChan.

El firmware escanea redes visibles antes de conectar. Si `GoogleGuest` o `Google-Guest` no aparecen, las salta y pasa a la siguiente red visible configurada. Si ninguna red configurada aparece, reescanea antes de fallar.

Para validar la configuracion local sin imprimir secretos:

```bash
python3 tools/validate_local_config.py
```

Para validar tambien una sesion Gemini Live WebSocket real:

```bash
python3 tools/validate_local_config.py --live
```

La descripcion completa de arranque, Wi-Fi, CSV y sesiones Live esta en `docs/firmware.md`.
