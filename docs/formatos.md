# Formatos de archivo de los Epics 2, 3 y 4 (Sprints 2 a 4)

Epic 2 (`scanner_match`): secciones 1 a 4. Epic 3 (`scanner_reconstruct`): secciones 5 a 9. Epic 4 (`scanner_dense`, `scanner_texture`, `scanner_view`, `scanner_accuracy`): secciones 10 a 15.

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

### Selección de pares (opción agregada en el Sprint 3)

La sección `matching` tiene dos claves más. Ambas solo amplían la selección de pares: no cambian el ratio test, `ransac_threshold_px`, `min_inliers` ni la calidad. Las dos aparecen también en `params` del `matching_report.json`.

| Clave | Por defecto | Efecto |
|---|---|---|
| `neighbor_index_window` | 0 | Además de los pares por orientación (HU-10), cada vista se empareja con las N siguientes según su posición entre las fotos usadas. Las fotos excluidas no dejan huecos. Va de 0 a 10. |
| `ring_closure` | false | Con la ventana activa, la cuenta da la vuelta: las últimas vistas se emparejan con las primeras. |

Con `0` / `false` (los valores por defecto) la selección es exactamente la del Sprint 2. El motivo y el resultado de la prueba están en `docs/sprint3-reporte.md`.

---

# Epic 3 (Sprint 3): reconstrucción dispersa

`scanner_reconstruct` lee de la sesión:
- `manifest.json`;
- `matching_report.json`;
- los `matches\pair_###_###.csv` de los pares aceptados.

Escribe en `sessions\<id>\reconstruction\`:

```
reconstruction\
  reconstruction.json          cámaras, intrínsecos y nube (entrada del Sprint 4)
  reconstruction_report.json   errores por cámara antes/después del BA, puntos, tiempos
  sparse_cloud.ply             nube con color y centros de cámara (solo para verificar)
  debug_top.jpg, debug_side.jpg   solo con --debug
```

Todos los archivos se escriben de forma segura (temporal + renombrado).

## 5. Convenciones

- **Cuaterniones** `[w, x, y, z]` de Hamilton, el mismo orden que el manifiesto, COLMAP y Ceres (`ceres::QuaternionToRotation`, `ceres::QuaternionRotatePoint`). No es el orden de almacenamiento de `Eigen::Quaternion`.
- **Pose mundo → cámara:** `x_cam = R·X + t`, con centro de la cámara `C = −Rᵀ·t`.
  - Ejes de la cámara: x a la derecha, y hacia abajo, z hacia adelante.
  - Es la convención de COLMAP, lo que facilita la exportación del Sprint 4.
- **Escala arbitraria (monocular).** Se fija con la distancia entre los centros del par inicial, que vale 1. Como el bundle adjustment solo fija una componente de la traslación de la segunda cámara, al final se reescala toda la reconstrucción para que esa distancia sea 1 exacta. La escala métrica es del Sprint 4 (HU-22).
- **Origen:** la primera cámara del par inicial queda en el origen con rotación identidad.
- **Píxeles:** imagen original sin aplicar la orientación EXIF; el centro del píxel (0, 0) está en (0, 0). Es la misma convención que `features\*.feat`.
- **Modelo de cámara** (una sola cámara compartida):
  - `a = x/z`, `b = y/z`, `r² = a² + b²`;
  - `d = 1 + k1·r² + k2·r⁴`;
  - `u = f·d·a + cx`, `v = f·d·b + cy`.
  - El punto principal queda fijo en el centro de la imagen.

## 6. `reconstruction.json` (versión de esquema 1)

```json
{
  "schema_version": 1,
  "session_id": "20261121-142259-tenis",
  "generated_at": "2026-11-21T16:10:00-04:00",
  "conventions": { "quaternion": "...", "pose": "...", "scale": "..." },
  "params": { "...": "sección reconstruction usada" },
  "intrinsics": { "model": "radial k1, k2; ...", "f": 3079.27, "k1": 0.105, "k2": -0.280,
                  "cx": 2000, "cy": 1500, "width": 4000, "height": 3000 },
  "cameras": [
    { "index": 1, "registered": true, "reason": null,
      "q": [1, 0, 0, 0], "t": [0, 0, 0], "center": [0, 0, 0] },
    { "index": 2, "registered": true, "reason": null,
      "q": [0.99617, 0.06181, -0.00481, 0.06160], "t": [0.00810, 0.86826, 0.49605],
      "center": [-0.12262, -0.91483, -0.38477] },
    { "index": 9, "registered": false, "reason": "componente_desconectado",
      "q": null, "t": null, "center": null }
  ],
  "points": [
    { "id": 0, "xyz": [0.7906, -0.6352, 7.7240], "rgb": [16, 26, 39], "track_length": 4, "error_px": 0.603,
      "observations": [[1, 4498, 2316.526, 1246.062], [2, 4036, 2326.015, 1265.237],
                       [4, 3389, 2274.410, 1916.583], [5, 3108, 2314.339, 1909.218]] }
  ]
}
```

**`cameras`** — todas las fotos candidatas, en orden de índice.
- Las registradas traen `q`, `t` y `center` (este último es redundante, para leerlo cómodo).
- Las no registradas traen `reason`, que puede ser:

| Motivo | Significado |
|---|---|
| `pocas_correspondencias_2d3d` | menos de `pnp_min_inliers` correspondencias con puntos 3D al terminar el registro |
| `pnp_fallido` | `solvePnPRansac` no encontró solución |
| `pnp_pocos_inliers` | PnP con menos de `pnp_min_inliers` inliers |
| `componente_desconectado` | la foto no está conectada con el par inicial en el grafo de pares aceptados |
| `sin_par_inicial` | ningún par cumplió las condiciones de HU-13 |

**`points`** — la nube final, ya filtrada por HU-17.
- `rgb`: promedio de los píxeles de sus observaciones.
- `track_length`: número de observaciones.
- `error_px`: error de reproyección medio.
- `observations`: lista de `[foto, índice del punto en features\img_###.feat, x, y]`.

Al leer el archivo no se recuperan las pistas completas (`point.track = −1`). Hay prueba de ida y vuelta (`ReconstructionIo.RoundTripThroughFile`).

## 7. `reconstruction_report.json` (versión de esquema 1)

| Campo | Contenido |
|---|---|
| `params`, `solver` | Parámetros usados y resolvedor lineal de Ceres (`SPARSE_SCHUR` si hay biblioteca dispersa; si no, `DENSE_SCHUR`). |
| `intrinsics.initial` / `.final` | `f`, `k1`, `k2`, `cx`, `cy` y el tamaño, antes (K del Sprint 2) y después del bundle adjustment. |
| `initial_pair` | `i`, `j` y `evaluated`: cada par evaluado con `n_inliers`, `front_ratio`, `median_angle_deg` y `result` (`elegido`, `pocos_inliers`, `pocos_puntos_delante`, `angulo_mediano_bajo`). |
| `excluded_photos` | Fotos fuera del flujo (`rechazada_calidad` y las excluidas por `scanner_match`). |
| `cameras` | Por foto: `registered`, `reason`, `before` y `after` con `n`, `mean_px`, `rms_px` y `max_px` (`null` si no está registrada). |
| `global` | `before` / `after` con la misma estructura, sobre todas las observaciones. |
| `points` | `tracks` y `tracks_discarded_inconsistent`; `before_filter` y `after_filter`; detalle de los dos filtrados; `track_length` (mínimo, mediana, media y máximo). |
| `bundle_adjustment` | Cada corrida: `stage` (`incremental`, `final`, `final_2`), `iterations`, costo inicial y final, RMS antes y después, `retriangulated`, `termination` y `time_ms`. |
| `imu_consistency` | Por cada par de cámaras registradas consecutivas por índice, el ángulo de rotación relativa reconstruido frente a `2·acos|q_i·q_j|` del manifiesto. Incluye la media y el máximo de la diferencia absoluta. Es solo informativo. |
| `ring_geometry` | Centroide de los centros de cámara y distancia de cada centro a él (media, desviación, mínimo y máximo). |
| `timing_ms` | `load`, `tracks`, `reconstruction` (con los BA), `bundle_adjustment`, `colors`, `outputs` y `total`. |
| `warnings`, `summary` | Avisos; cámaras candidatas, registradas y no registradas; puntos; RMS antes y después. |

"Antes" y "después" del BA:
- **"Antes"** es el estado justo antes del BA final, cuando termina la fase incremental. Como hay BA intermedios (`ba_every`), ese estado ya está parcialmente refinado.
- **"Después"** es el estado tras el segundo BA y el filtrado final.

Las pistas de solo 2 vistas existen durante la fase incremental y desaparecen en el filtrado (HU-17, `min_views` = 3). Por eso el error "después" se calcula sobre menos observaciones.

## 8. `sparse_cloud.ply`

Es un PLY ASCII 1.0 con `element vertex N` y las propiedades `float x, y, z` y `uchar red, green, blue`.
- Los primeros vértices son los puntos de la nube, con su color.
- Los últimos son los centros de las cámaras registradas, en magenta (255, 0, 255). Un comentario de la cabecera dice cuántos son.
- Se abre en MeshLab o CloudCompare.
- Sirve solo para verificar; no es la entrada del Sprint 4 (para eso está `reconstruction.json`).

## 9. Imágenes de depuración (solo con `--debug`)

Las dos imágenes son vistas ortográficas de la nube (con su color) y de las cámaras. Los ejes se obtienen por PCA de los centros de cámara.
- **`debug_top.jpg`:** vista sobre el plano del anillo.
- **`debug_side.jpg`:** vista con la normal del anillo hacia arriba. El signo de la normal es arbitrario.

Cada cámara aparece como un círculo rojo con su índice y una línea que marca su eje óptico. El encuadre usa los percentiles 2–98 de los puntos y todas las cámaras.

### Opciones T0b de `scanner_match` (Sprint 4)

Tres claves más en `matching`, **apagadas por defecto**: con estos valores el resultado es el del Sprint 2. El motivo y las mediciones están en `docs/sprint4-reporte.md`, sección T0.

| Clave | Por defecto | Efecto |
|---|---|---|
| `sift_contrast_threshold` | 0,04 | `contrastThreshold` de `cv::SIFT` (0,04 es el valor de OpenCV) |
| `guided_matching` | false | Un par **ya aceptado** se vuelve a emparejar solo dentro de la banda epipolar de su F (Sampson ≤ `ransac_threshold_px`), con el mismo ratio test entre las candidatas de la banda. Se conserva si sigue aceptado y tiene más inliers |
| `mutual_check` | false | Solo se conservan las correspondencias recíprocas (A→B y B→A), en el emparejamiento normal y en el guiado |

---

# Epic 4 (Sprint 4): densificación, malla, textura, visor y precisión

```
sessions\<id>\
  reconstruction\...                 (Sprint 3)
  colmap_ours\sparse\0\              modelo propio exportado a formato COLMAP (texto)
  dense\                             espacio de trabajo de COLMAP (images, stereo, ...) y fused.ply
  model\    mesh_raw.ply, mesh.ply, model.obj, model.mtl, texture.png, model_report.json, colmap.log
  colmap_ref\                        referencia de COLMAP: database.db, image_list.txt, sparse\, sparse_txt\
  accuracy\accuracy_report.json, colmap.log
  dense_colmap\, model_colmap\       lo mismo con las poses de COLMAP (--poses colmap, comparación)
```

Todos los archivos propios se escriben de forma segura (temporal + renombrado). La carpeta de cada sesión está ignorada por git.

## 10. Modelo propio en formato COLMAP: `colmap_ours\sparse\0\`

Son los tres archivos de texto de COLMAP (`cameras.txt`, `images.txt`, `points3D.txt`), escritos por `colmap_io.cpp` a partir de `reconstruction\reconstruction.json`.

- **`cameras.txt`:** una sola cámara, `1 RADIAL ancho alto f cx cy k1 k2`, con `cx` y `cy` desplazados +0,5 (COLMAP pone el centro del primer píxel en (0,5; 0,5)).
- **`images.txt`:** solo las cámaras **registradas**.
  - `IMAGE_ID` es el índice de la foto (`img_###.jpg`).
  - `QW QX QY QZ TX TY TZ` son la pose propia sin transformar (las convenciones coinciden).
  - En la segunda línea van los puntos 2D `X Y POINT3D_ID`, con +0,5.
- **`points3D.txt`:** `POINT3D_ID X Y Z R G B ERROR` y la pista `(IMAGE_ID, POINT2D_IDX)`. `ERROR` es el error de reproyección medio del punto.

`readColmapText` lee también los modelos de COLMAP (`SIMPLE_PINHOLE`, `PINHOLE`, `SIMPLE_RADIAL`, `RADIAL`, una sola cámara) y resta el 0,5.

## 11. Mallas: `dense\fused.ply`, `model\mesh_raw.ply`, `model\mesh.ply`

- **`fused.ply`** (nube densa) y **`mesh_raw.ply`** (Poisson) los escribe COLMAP: PLY binario con `x y z`, normales en la nube y colores `red green blue`.
- **`mesh.ply`** es la malla limpia:
  - PLY binary_little_endian;
  - propiedades `float x y z`, más `nx ny nz` y `uchar red green blue` si la malla original las tenía;
  - caras como `list uchar int`.

La limpieza (`cleanMesh`) quita:
- las caras degeneradas (índices repetidos o área ≈ 0);
- las componentes con menos de `mesh.min_component_faces` caras;
- los vértices sin caras.

## 12. OBJ, MTL y textura: `model\model.obj`, `model.mtl`, `texture.png`

```
# Escaner 3D por Fotogrametria (Sprint 4)
# unidad: mm; factor de escala aplicado: 312.5        (o "escala arbitraria (monocular); factor 1")
mtllib model.mtl
v x y z                 (ya escalados)
vt u v                  (uno por vértice de cara: 3 por cara)
vn nx ny nz             (solo si la malla trae normales)
usemtl material0
f v/vt[/vn] v/vt[/vn] v/vt[/vn]      (índices base 1)
```

- **MTL:** `newmtl material0` con `map_Kd texture.png`, por ruta relativa.
- **UV:**
  - en [0, 1], con v hacia arriba (convención OBJ: v = 1 − y_atlas / alto);
  - las caras sin cámara apuntan a un bloque gris neutro del atlas.
- **Atlas:**
  - cartas (caras vecinas con la misma cámara) recortadas de la foto **sin distorsión**, con un relleno de `texture.padding_px` píxeles;
  - empaquetado por estantes en un ancho de `texture.atlas_max_size`;
  - si no caben, todas las cartas se reducen por el mismo factor (`chart_scale` en el reporte).
- **Escala** (HU-22):
  - `--scale-ref "x1,y1,z1;x2,y2,z2;D_mm"` aplica el factor `D / |p1 − p2|` a todos los vértices, y la unidad pasa a ser mm;
  - `--scale s` aplica un factor directo;
  - sin ninguna de las dos, la escala es la arbitraria de la reconstrucción (distancia 1 entre las cámaras del par inicial).
- **Impresión 3D:** el reporte dice si la malla es cerrada y cuántas aristas abiertas tiene. No se repara más allá de la limpieza.

## 13. `model\model_report.json`

Cada herramienta escribe su sección sin borrar las demás:

| Sección | Campos |
|---|---|
| `dense` | `poses` (`ours` o `colmap`), `params`, versión de `colmap`, `cameras`, `sparse_points`, `dense_points`, `fused_ply`, `undistort_ms`, `patch_match_ms`, `fusion_ms` |
| `mesh` | `raw_vertices`, `raw_faces`; `cleaning` (`degenerate_faces`, `small_components`, `small_component_faces`, `unreferenced_vertices`); `vertices`, `faces`, `components`, `open_edges`, `closed`, `area` (unidades de la reconstrucción), `poisson_ms`, `clean_ms` |
| `texture` | `params`, `faces`, `textured_faces`, `textured_pct`, `faces_per_camera`, `charts`, `atlas_width`, `atlas_height`, `chart_scale`, `time_ms` |
| `export` | `obj`, `scale`, `unit` (`mm`, `arbitraria` o `arbitraria x factor`), `vertices`, `faces`, `closed`, `open_edges` |

## 14. `accuracy\accuracy_report.json`

| Campo | Contenido |
|---|---|
| `cameras_ours`, `cameras_colmap`, `cameras_common` | Cámaras registradas en cada lado y comunes (emparejadas por nombre de imagen) |
| `reliable`, `note` | `false` con menos de `accuracy.min_common_cameras` comunes: entonces no se dan errores de pose (`null`) |
| `alignment` | Similitud de Umeyama propio → COLMAP (con RANSAC si hay más de 8 comunes; umbral 5 % de la dispersión de los centros comunes). Incluye `scale`, `ring_radius`, `inliers`, `position_error` y `rotation_error_deg` |
| `alignment.ring_radius` | Distancia media de **todas** las cámaras de COLMAP a su centroide |
| `alignment.position_error` | Media, RMS y máximo en unidades de COLMAP; `position_error_pct_radius` da lo mismo en % de `ring_radius` |
| `alignment.rotation_error_deg` | Media y máximo |
| `intrinsics` | `f_ours`, `f_colmap`, `f_diff_px`, `f_diff_pct`, `k1`/`k2` de cada uno |
| `reprojection_error_px` | Media y RMS de `ours` y `colmap`, con la misma fórmula (`computeCameraErrors`) sobre el modelo de cada uno |
| `sparse_points` | Cantidad de cada lado; distancia media y mediana de cada punto propio alineado al punto de COLMAP más cercano, y la mediana en % del radio |
| `per_camera` | Por cámara común: `index`, `position_error`, `rotation_error_deg` y `alignment_inlier` |
| `params`, `reference_ms`, `colmap_reference` | Parámetros, tiempo de la referencia y su carpeta |

## 15. Imágenes del visor (`scanner_view --render`)

- Una imagen de `viewer.width` × `viewer.height` con dos vistas: **general** (órbita alrededor del centroide) y **detalle** (zoom ×4 sobre el punto del modelo más cercano al centro de la vista general, o sobre el punto elegido con clic en la ventana).
- Renderizado por software con z-buffer: los puntos son cuadrados de 2×2 con su color; los triángulos llevan la textura del atlas por coordenadas baricéntricas corregidas por perspectiva.
- Los centros de cámara se dibujan en magenta.

### Búsqueda de focal del par inicial (v1.0.1)

`reconstruction` tiene cuatro claves nuevas, descritas en `docs/sprint3-reporte.md`, sección «Corrección posterior»:

| Clave | Por defecto |
|---|---|
| `init_focal_search` | true |
| `init_focal_factor_min` | 0,8 |
| `init_focal_factor_max` | 1,4 |
| `init_focal_factor_step` | 0,05 |

`reconstruction_report.json` agrega en `initial_pair` tres campos:
- `points`: puntos triangulados del par inicial;
- `focal_factor`: 1 si no hizo falta la rejilla;
- `focal_search`: lista de `{factor, points, median_error_px}`, vacía si la K original triangulaba.
