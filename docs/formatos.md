# Formatos de archivo del Epic 2 (Sprint 2)

`scanner_match` escribe tres tipos de archivo dentro de la carpeta de cada sesión (`sessions\<id>\`, ignorada por git):

```
sessions\<id>\
  manifest.json              (Sprint 1, solo se lee)
  img_###.jpg                (Sprint 1, solo se leen)
  features\img_###.feat      características SIFT de cada foto usada
  matches\pair_###_###.csv   inliers de cada par ACEPTADO
  matching_report.json       resultado de toda la corrida
  debug\*.jpg                solo con --debug
```

Todos se escriben de forma segura: primero `<archivo>.tmp` y luego se renombra (`writeFileAtomic`).
La carpeta `matches\` se vacía al empezar cada corrida, para que un par rechazado nunca conserve inliers de una corrida anterior.

**Convención de coordenadas (todos los archivos).**
- Las coordenadas están en píxeles de la imagen **original** (p. ej. 4000×3000), con el centro del píxel (0, 0) en (0, 0). La imagen se lee sin aplicar la orientación EXIF (`IMREAD_IGNORE_ORIENTATION`), igual que en el Sprint 1.
- Si SIFT corrió sobre una imagen reducida con escala `s`, cada punto se lleva a la original con `x_orig = (x_trab + 0,5) / s − 0,5`. Es la división entre `s` con la corrección de medio píxel de `cv::resize`.

## 1. Características: `features\img_###.feat` (binario, versión 1)

Little-endian, sin relleno entre campos. Lectura y escritura: `readFeatures` / `writeFeatures` (`server/src/features.cpp`).

| Desplazamiento | Tipo | Campo |
|---|---|---|
| 0 | `char[4]` | firma `"E3DF"` |
| 4 | `uint32` | versión del formato (= 1) |
| 8 | `int32` | ancho original (px) |
| 12 | `int32` | alto original (px) |
| 16 | `int32` | ancho de trabajo (imagen sobre la que corrió SIFT) |
| 20 | `int32` | alto de trabajo |
| 24 | `float64` | escala `s` = trabajo / original (≤ 1; nunca se amplía) |
| 32 | `uint32` | `n`, número de puntos |
| 36 | `uint32` | dimensión del descriptor (= 128) |
| 40 | `n` × 24 bytes | puntos: `float32 x, y, size, angle, response; int32 octave` |
| 40 + 24n | `n` × 128 × `float32` | descriptores SIFT por filas (fila `k` = punto `k`) |

- `x`, `y` y `size` están en píxeles de la imagen original.
- `angle`, `response` y `octave` son los de `cv::KeyPoint`; `octave` se refiere a la imagen de trabajo.
- El tamaño total debe ser exactamente `40 + n × (24 + 512)` bytes; si no, la lectura falla (`FeatureFileError`).
- El índice de un punto es su posición en el archivo (0 … n−1). Los archivos de correspondencias usan ese índice.

## 2. Correspondencias inlier: `matches\pair_iii_jjj.csv`

Texto UTF-8, separado por comas, con una línea de cabecera fija. Solo existe para los pares **aceptados**. `iii < jjj` son los índices de las fotos en el manifiesto, con tres cifras. Lectura y escritura: `readInliers` / `writeInliers` (`server/src/matching_report.cpp`).

```
index_i,index_j,x_i,y_i,x_j,y_j,distance,sampson_px
21,14,452.065277,1730.83484,429.840485,1876.37061,84.6286011,0.98352209860779805
```

| Columna | Significado |
|---|---|
| `index_i` | índice del punto en `features\img_iii.feat` |
| `index_j` | índice del punto en `features\img_jjj.feat` |
| `x_i, y_i` | punto en la foto `i` (px de la imagen original) |
| `x_j, y_j` | punto en la foto `j` (px de la imagen original) |
| `distance` | distancia euclidiana entre los dos descriptores SIFT |
| `sampson_px` | distancia de Sampson del par de puntos con la `F` del par (≤ `ransac_threshold_px`) |

- Los `float` se escriben con `%.9g` y `sampson_px` con `%.17g`, así que la ida y vuelta es exacta.
- La convención epipolar es `x_jᵀ F x_i = 0`.

## 3. Reporte: `matching_report.json` (versión de esquema 1)

Lectura y escritura: `readMatchingReport` / `writeMatchingReport`. Un archivo sin alguno de los campos de abajo se rechaza (`std::invalid_argument`).

```json
{
  "schema_version": 1,
  "session_id": "20261120-212147-llama",
  "generated_at": "2026-11-20T23:05:12-04:00",
  "params": { "...": "la sección matching usada (sección 3.2 del Sprint 2)" },
  "views": [
    { "index": 1, "file": "img_001.jpg", "features_file": "features/img_001.feat",
      "n_features": 5680, "width": 4000, "height": 3000, "work_width": 2000, "work_height": 1500,
      "focal_px": 2888.84, "focal_source": "focal_35mm",
      "K": [[2888.84, 0, 2000], [0, 2888.84, 1500], [0, 0, 1]],
      "load_ms": 44.1, "extract_ms": 374.0 }
  ],
  "excluded_views": [ { "index": 9, "reason": "rechazada_calidad" } ],
  "pairs": [
    { "i": 1, "j": 2, "angle_deg": 17.4, "n_matches": 643, "n_inliers": 298,
      "inlier_ratio": 0.463, "mean_sampson_px": 0.433, "accepted": true, "reasons": [],
      "rank2_enforced": false, "F": [[...], [...], [...]], "E": [[...], [...], [...]],
      "match_ms": 310.2, "geometry_ms": 15.7, "inliers_file": "matches/pair_001_002.csv" }
  ],
  "warnings": [],
  "timing": { "extraction_ms": 7800.0, "matching_ms": 12700.0, "total_ms": 20800.0 },
  "summary": { "views_used": 20, "views_excluded": 0, "pairs_all_vs_all": 190,
               "pairs_selected": 44, "pairs_accepted": 5, "pairs_rejected": 39 }
}
```

**`views`** — fotos usadas, en orden de índice.
- `K` es la matriz intrínseca **aproximada** de la sección 3.3 (`f = focal_35mm × diagonal / 43,27`, punto principal en el centro).
- `focal_source` vale `"fallback"` si no había `focal_35mm` y se usó `f = fallback_focal_factor × lado mayor`. En ese caso también hay un aviso en `warnings`.
- `load_ms` es la lectura del JPEG; `extract_ms`, la preparación (gris, reducción, CLAHE) más SIFT.

**`excluded_views`** — fotos del manifiesto que no se usaron. Motivos:
- `rechazada_calidad`: la rechazó HU-06 en el Sprint 1;
- `sin_orientacion`: no trae `orientation_q`;
- `sin_dimensiones`;
- `imagen_ilegible`.

**`pairs`** — los pares vecinos seleccionados (HU-10), ordenados por `(i, j)`.
- `angle_deg`: diferencia angular entre las orientaciones registradas.
- `n_matches`: correspondencias que pasaron el ratio test.
- `n_inliers` / `inlier_ratio` / `mean_sampson_px`: inliers según la distancia de Sampson propia (≤ `ransac_threshold_px`), y su error medio.
- `F`: matriz fundamental de `findFundamentalMat` (FM_RANSAC), proyectada a rango 2 por SVD. Escala de OpenCV, normalmente `F[2][2] = 1`.
  - `rank2_enforced = true` indica que la F de OpenCV no tenía rango 2 numérico (σ₃/σ₁ > 1e-12) antes de proyectarla.
- `E`: `K_jᵀ F K_i` proyectada a la variedad esencial, con valores singulares `(s, s, 0)` y `s` = promedio de los dos primeros. Escala arbitraria (la de `F`).
- `F` y `E` valen `null` si F no se pudo estimar (menos de 8 correspondencias o fallo de RANSAC).
- `accepted` / `reasons`: validación de HU-12. Los motivos son `f_no_estimada`, `pocos_inliers` y `razon_inliers_baja`, y puede haber varios.
- `inliers_file`: solo en pares aceptados. En los rechazados es siempre `null`: **el Sprint 3 debe usar únicamente los pares con `inliers_file` distinto de `null`.**
- `match_ms` / `geometry_ms`: tiempo de emparejamiento (kNN + ratio) y de estimación de F/E.

**`summary`** — `pairs_all_vs_all = n(n−1)/2` con las vistas usadas (190 para 20 fotos), para compararlo con `pairs_selected`.

## 4. Imágenes de depuración: `debug\*.jpg` (solo con `--debug`)

Cada imagen muestra las dos fotos lado a lado, reducidas a 1600 px en el lado mayor. Las correspondencias candidatas se dibujan en rojo y los inliers en verde (como máximo unas 400 líneas).

| Archivo | Contenido |
|---|---|
| `accepted_iii_jjj.jpg` | el par aceptado con menor razón de inliers (el más débil) |
| `rejected_iii_jjj.jpg` | el par rechazado con más inliers (el más cercano a pasar) |
| `far_iii_jjj.jpg` | solo si no hubo rechazados: el par de mayor ángulo, evaluado aparte y fuera de `pairs` |
