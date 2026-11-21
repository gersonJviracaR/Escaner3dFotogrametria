# Contrato HTTP del servidor y esquema del manifiesto

El servidor (`server/build/Release/scanner_server.exe`) escucha en `0.0.0.0:<puerto>` (8080 por defecto). Al arrancar imprime las IPv4 locales de la PC, que el usuario escribe en la app como `IP:puerto`.

- HTTP sin cifrar, pensado para la Wi-Fi local (la app habilita `usesCleartextTraffic`).
- Todas las respuestas son JSON (`application/json; charset=utf-8`).
- Los errores siempre tienen la forma `{"error": "mensaje"}`.

## Endpoints

| Método y ruta | Descripción | Respuesta correcta |
|---|---|---|
| `GET /health` | Comprueba que el servidor responde. | `200 {"status":"ok","version":"0.1.0"}` |
| `POST /sessions` | Crea una sesión. El cuerpo JSON es opcional: `{"name":"estatua01","device":{"model":"...","android":"..."}}`. | `201 {"session_id":"20261110-153000-estatua01"}` |
| `POST /sessions/{id}/photos` | `multipart/form-data` con el campo `image` (JPEG) y el campo `metadata` (JSON, ver abajo). Guarda la foto, la valida y devuelve el resultado. Si el `index` ya existe, **reemplaza** la foto. | `200` con el resultado de validación |
| `POST /sessions/{id}/complete` | Escribe `manifest.json` y devuelve el resumen. Se puede llamar otra vez después de repetir tomas. | `200 {"total":20,"accepted":19,"rejected":[7]}` |
| `GET /sessions/{id}/validation` | Estado de validación actual de toda la sesión. | `200 {"session_id":"...","photos":[...],"summary":{...}}` |

### Códigos de error

| Código | Cuándo |
|---|---|
| `400` | Cuerpo JSON inválido; falta `image` o `metadata`; `metadata` mal formado (p. ej. `index` fuera de 1–999, cuaternio sin 4 números); JPEG ilegible. |
| `404` | La sesión no existe, o la ruta no existe. |
| `413` | La foto supera el máximo (64 MB por defecto, `max_photo_mb` en la configuración). |
| `500` | Error interno inesperado (queda registrado en la consola del servidor). |

### Identificador de sesión

`AAAAMMDD-HHMMSS-<nombre>`, con la hora local del servidor. El nombre se limpia a `[A-Za-z0-9_-]` (lo demás pasa a `_`) y se recorta a 40 caracteres. Sin nombre, el id es solo `AAAAMMDD-HHMMSS`. Si el id ya existe se agrega `-2`, `-3`, etc.

### `metadata` de cada foto

```json
{
  "index": 1,
  "captured_at": "2026-11-10T15:31:02-04:00",
  "width": 4000,
  "height": 3000,
  "focal_mm": 5.6,
  "focal_35mm": 24,
  "orientation_q": [0.98, 0.01, 0.17, 0.02]
}
```

| Campo | Tipo | Notas |
|---|---|---|
| `index` | entero 1–999 | Obligatorio. Determina el archivo `img_###.jpg`. |
| `captured_at` | texto ISO 8601 | Hora del celular con desfase. |
| `width`, `height` | entero ≥ 0 | Los declara la app; **prevalecen los de la imagen decodificada por el servidor** y una discrepancia se registra en la consola. |
| `focal_mm`, `focal_35mm` | número o `null` | Del EXIF (`FocalLength`, `FocalLengthIn35mmFilm`). `null` si el celular no los entrega. |
| `orientation_q` | `[w, x, y, z]` o `null` | Cuaternio de `TYPE_GAME_ROTATION_VECTOR` (respaldo: `TYPE_ROTATION_VECTOR`) al disparar. `null` si no hay sensor. |

En este sprint no se calcula la matriz K: solo se guardan los datos crudos (Epics 2 y 3).

### Resultado de validación de una foto

```json
{ "index": 7, "accepted": false, "sharpness": 41.2, "overexposed_ratio": 0.01, "reasons": ["borrosa"] }
```

`reasons` puede contener `"borrosa"` y/o `"saturada"`.

- **Nitidez (`sharpness`)**: la imagen se pasa a gris y se redimensiona para que su lado mayor mida 1000 px. Luego se calcula la varianza del Laplaciano **solo en el 50 % central** (la mitad del ancho y la mitad del alto, centradas): el objeto suele estar al centro, y un fondo liso o desenfocado no debe hacer pasar por borrosa una foto nítida. Es `"borrosa"` si es menor que `quality.sharpness_min` (94, calibrado con fotos reales; ver `docs/sprint1-reporte.md`).
- **Saturación (`overexposed_ratio`)**: fracción de píxeles con gris ≥ 250 (medida sobre la misma imagen redimensionada). Es `"saturada"` si es mayor que `quality.overexposed_max`.
- Se mide la cuadrícula de píxeles tal como se guardó: se ignora la etiqueta de orientación EXIF.

## Estructura en disco

```
sessions/20261110-153000-estatua01/
├── img_001.jpg        # bytes originales del JPEG (con su EXIF)
├── img_002.jpg
├── ...
└── manifest.json      # se escribe en complete (archivo temporal + renombrado)
```

## `manifest.json` (schema_version 1)

```json
{
  "schema_version": 1,
  "session_id": "20261110-153000-estatua01",
  "created_at": "2026-11-10T15:30:00-04:00",
  "device": { "model": "...", "android": "..." },
  "photos": [
    {
      "index": 1,
      "file": "img_001.jpg",
      "width": 4000, "height": 3000,
      "focal_mm": 5.6, "focal_35mm": 24,
      "orientation_q": [0.98, 0.01, 0.17, 0.02],
      "captured_at": "2026-11-10T15:31:02-04:00",
      "quality": { "sharpness": 152.3, "overexposed_ratio": 0.02, "accepted": true, "reasons": [] }
    }
  ],
  "summary": { "total": 20, "accepted": 19, "rejected": [7] }
}
```

Las fotos están ordenadas por `index`, y `summary.rejected` lista los índices rechazados en orden ascendente.

## Configuración (`server/config.default.json`)

```json
{
  "port": 8080,
  "data_dir": "sessions",
  "max_photo_mb": 64,
  "quality": { "sharpness_min": 94.0, "overexposed_max": 0.15 }
}
```

Prioridad, de menor a mayor: valores internos < `config.default.json` (junto al ejecutable) < archivo `--config` < opciones `--port` / `--data-dir`.

## Ejemplo con curl.exe

```powershell
curl.exe http://127.0.0.1:8080/health
curl.exe -X POST -H "Content-Type: application/json" --data-binary "@session.json" http://127.0.0.1:8080/sessions
curl.exe -X POST -F "image=@img.jpg;type=image/jpeg" -F "metadata=<meta.json;type=application/json" http://127.0.0.1:8080/sessions/<id>/photos
curl.exe -X POST http://127.0.0.1:8080/sessions/<id>/complete
```

`scripts/smoke-test.ps1` automatiza este flujo.
