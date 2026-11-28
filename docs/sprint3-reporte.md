# Reporte del Sprint 3 (Epic 3): reconstrucción dispersa y bundle adjustment

**Meta:** a partir de los pares validados de una sesión, el servidor reconstruye una nube de puntos dispersa con las poses de cámara optimizadas y reporta el error de reproyección por cámara.

Rama `sprint-3/epic-3-reconstruccion-dispersa`, integrada en `main` con la etiqueta `v0.3-sprint3`.
Todos los números salen de ejecuciones reales en la PC del equipo:
- las pruebas automáticas;
- `scanner_match` y `scanner_reconstruct` sobre la sesión `20261121-142259-tenis`;
- COLMAP como diagnóstico.

## 1. Historias y puntos

| ID | Historia | Pts | Estado |
|---|---|---|---|
| HU-13 | Como operador quiero elegir un par inicial y recuperar su pose relativa (R,t). | 3 | Hecha con pruebas verdes |
| HU-14 | Como operador quiero triangular puntos 3D con coordenadas homogéneas (DLT). | 3 | Hecha con pruebas verdes |
| HU-15 | Como operador quiero registrar las demás cámaras con PnP-RANSAC y triangular nuevos puntos. | 5 | Hecha con pruebas verdes |
| HU-16 | Como operador quiero refinar poses y puntos con bundle adjustment en Ceres. | 5 | Hecha con pruebas verdes |
| HU-17 | Como operador quiero descartar puntos visibles en menos de 3 cámaras o con error de reproyección alto. | 3 | Hecha con pruebas verdes |
| HU-18 | Como operador quiero ver el error de reproyección de cada cámara. | 1 | Hecha con pruebas verdes |

**Puntos completados: 20 de 20**, en el sentido de los criterios de aceptación, que se prueban con escenas sintéticas.

El **resultado sobre la sesión real es parcial**: se registran **11 de 34 cámaras**. La sección 5 explica la causa. COLMAP registra 31 de 34 con las mismas fotos, lo que muestra que el cuello de botella es la densidad del emparejamiento del Sprint 2, no la reconstrucción.

### Criterios de aceptación y evidencia

| HU | Criterio (resumen) | Evidencia |
|---|---|---|
| HU-13 | (R, t) con ‖t‖ = 1 y la mayoría de los puntos delante de ambas cámaras | `Initialization.*`: rotación < 0,5°, dirección de t < 1°, más del 90 % delante. En `tenis`: par 1-2, el 99,9 % delante, ángulo mediano 6,0° |
| HU-14 | DLT homogénea; se descartan puntos detrás o con ángulo pequeño | `Triangulation.*`: sin ruido, error < 1e-6; con más vistas, menor error; ángulo comprobado a mano |
| HU-15 | Registro con PnP-RANSAC si se supera el mínimo de inliers; puntos nuevos con ≥ 2 cámaras | `Registration.*`: anillo de 12 cámaras completo (centros < 2 % del radio, rotación < 1°); cámara con 6 correspondencias no registrada con su motivo; componente desconectado informado |
| HU-16 | El RMS baja y los cuaterniones siguen unitarios | `BundleAdjustment.*`: RMS al nivel del ruido; norma 1 ± 1e-9; calibre fijo; Huber resiste un atípico; f recuperada desde ±10 % con error < 2 %; f constante con `refine_focal: false` |
| HU-17 | Solo puntos con ≥ 3 vistas dentro del frustum y error bajo el umbral | `Visibility.*`: frustum (dentro, fuera por cada lado, detrás, borde exacto), `min_views`, error alto |
| HU-18 | Error medio y RMS por cámara y global, antes y después del BA | `ReconstructionReport.*`: comparado con un cálculo a mano; estructura completa del JSON |

## 2. Pruebas

| | Cantidad |
|---|---|
| Pruebas de los Sprints 1 y 2 (siguen verdes) | 107 |
| Pruebas nuevas del Sprint 3 | 69 |
| **Total (`ctest`)** | **176, todas verdes** |

Pruebas nuevas por grupo:

| Grupo | Pruebas | Grupo | Pruebas |
|---|---|---|---|
| `ReconstructionConfig` | 5 | `BundleAdjustment` (HU-16) | 7 |
| `CameraModel` | 7 | `Visibility` (HU-17) | 7 |
| `Triangulation` (HU-14) | 9 | `ReconstructionReport` (HU-18) | 4 |
| `Initialization` (HU-13) | 4 | `ReconstructionIo` | 7 |
| `Tracks` | 5 | `SessionReconstruction` (de extremo a extremo, en disco) | 3 |
| `Registration` (HU-15) | 5 | `IndexWindowPairs` y `MatchingConfig` (ventana de vecinos) | 5 + 1 |

Todas usan escenas sintéticas: puntos 3D conocidos, cámaras con K, R y t conocidos, y 0,3 px de ruido. La escena con distorsión verdadera (k1 = 0,10, k2 = −0,25) y la focal inicial desviada un 8 % reproduce el caso real.

## 3. Paso 0: grafo de vistas de `tenis` (antes de escribir código)

`scanner_match` con los parámetros por defecto, sobre 36 fotos (la 13 y la 19 rechazadas por borrosas en el Sprint 1):

| | Valor |
|---|---|
| Pares seleccionados / aceptados / rechazados | 105 / **62** / 43 (`pocos_inliers` 42, `razon_inliers_baja` 18) |
| Componentes conexos | **2**: 30 vistas {1–8, 14–18, 20–36} y 4 vistas {9, 10, 11, 12} |
| Grado por vista | mínimo 1, máximo 6, medio 3,65; las vistas 8, 9 y 12 tienen un solo vecino |
| Tiempo de `scanner_match` | 30,5 s |

Causa del corte, a la vista de las fotos 6–15:
- en las fotos 9–12 la cámara mira hacia la ventana: contraluz, zapatilla oscura sin detalle y fondo quemado;
- el paso 8-9 llega a 20/77 inliers;
- la foto 13, que unía 12 con 14, era borrosa.

Con `ransac_threshold_px = 2` se aceptaban 72 pares, pero el corte seguía igual.

**Decisión del usuario:** continuar con el componente de 30 vistas, sin cambiar parámetros.

## 4. Resultado final con `tenis` (parámetros por defecto)

Ejecución: `scanner_match --session ...` y luego `scanner_reconstruct --session ... --debug`.

### Cámaras

| | Valor |
|---|---|
| Candidatas | 34 (36 − 2 rechazadas por calidad) |
| **Registradas** | **11**: 1, 2, 3, 4, 5, 6, 7, 33, 34, 35, 36 |
| No registradas por `componente_desconectado` | 4: 9, 10, 11, 12 |
| No registradas por `pocas_correspondencias_2d3d` | 19: 8, 14–18 y 20–32 |
| Par inicial | 1-2: 1474 inliers, ángulo mediano 6,04°, 99,9 % de los puntos delante |
| Solver de Ceres | `DENSE_SCHUR` (el Ceres de vcpkg se compiló sin SuiteSparse ni Eigen disperso) |

Al terminar el registro, las cámaras restantes con más correspondencias 2D–3D eran la 8 (10) y la 32 (12), con un mínimo exigido de 20. Las demás tenían entre 1 y 3.

### Error de reproyección por cámara (px)

"Antes" es antes del BA final, al terminar la fase incremental; "después" es tras el segundo BA y el filtrado.

| Foto | Obs. antes | Medio antes | RMS antes | Máx. antes | Obs. después | Medio después | RMS después | Máx. después |
|---|---|---|---|---|---|---|---|---|
| 1 | 1515 | 0,259 | 0,348 | 2,50 | 363 | 0,403 | 0,482 | 1,46 |
| 2 | 1534 | 0,271 | 0,373 | 3,31 | 361 | 0,389 | 0,454 | 1,81 |
| 3 | 803 | 0,346 | 0,435 | 2,56 | 426 | 0,402 | 0,476 | 1,37 |
| 4 | 830 | 0,320 | 0,406 | 1,85 | 374 | 0,395 | 0,473 | 1,51 |
| 5 | 732 | 0,318 | 0,408 | 2,23 | 319 | 0,392 | 0,473 | 1,84 |
| 6 | 485 | 0,328 | 0,424 | 1,96 | 214 | 0,415 | 0,515 | 1,64 |
| 7 | 145 | 0,402 | 0,522 | 2,00 | 52 | 0,501 | 0,604 | 1,76 |
| 33 | 150 | 0,343 | 0,438 | 1,42 | 70 | 0,380 | 0,486 | 1,73 |
| 34 | 416 | 0,325 | 0,431 | 1,90 | 149 | 0,447 | 0,549 | 1,86 |
| 35 | 483 | 0,324 | 0,416 | 2,32 | 175 | 0,394 | 0,474 | 2,00 |
| 36 | 374 | 0,441 | 0,565 | 2,29 | 178 | 0,549 | 0,646 | 1,92 |
| **Global** | 7467 | 0,309 | **0,408** | 3,31 | 2681 | 0,412 | **0,497** | 2,00 |

El RMS global "después" (0,497) es mayor que el "antes" (0,408) porque las poblaciones son distintas: el filtrado de HU-17 quita las pistas de 2 vistas, que por construcción tienen error casi nulo. Dentro de cada corrida, el BA siempre bajó el RMS; por ejemplo, el segundo BA pasó de 0,509 a 0,498 px y el primero, de 1,354 a 0,267 px.

### Puntos

| | Valor |
|---|---|
| Pistas | 6719 (55 descartadas por inconsistentes) |
| Puntos antes del filtrado | 3124 |
| Puntos eliminados en el filtrado | 2390 con menos de 3 vistas; además, 9 observaciones con error > 2 px |
| **Puntos finales** | **734** |
| Longitud de pista (mínimo / mediana / media / máximo) | 3 / 3 / 3,65 / 9 |

### Intrínsecos

| | Inicial (EXIF) | Final (BA) |
|---|---|---|
| f | 2888,8 px | **3079,3 px** (+6,6 %) |
| k1 | 0 | 0,105 |
| k2 | 0 | −0,280 |

Comprobación independiente: COLMAP, con las mismas fotos y modelo `SIMPLE_RADIAL`, estimó **f = 3093,2 px**, a un 0,45 % de la nuestra. Las k no son comparables: COLMAP ajusta un solo coeficiente radial (k = −0,002).

### Comprobación con la IMU (solo informativa)

Se comparan 10 pares de cámaras registradas consecutivas por índice: el ángulo de rotación relativa reconstruido frente a `2·acos|q_i·q_j|` del manifiesto.
- Diferencia media: **0,28°**; máxima: **0,81°**.
- Ejemplos: 1-2 da 10,03° reconstruido frente a 9,71° de la IMU; 4-5, 11,90° frente a 11,34°; 7-33, 104,68° frente a 105,49°.
- La concordancia es buena: las rotaciones reconstruidas son coherentes con el celular.

### Geometría del anillo

Distancia de los centros de cámara a su centroide:
- media 5,38 y desviación 2,96 (mínimo 0,57, máximo 9,94), en la escala en que la distancia del par inicial vale 1;
- **esta medida no es representativa**: las 11 cámaras cubren solo un arco de ~115° del anillo (33→36→1→7: 95,8° entre consecutivas más ~18° entre 36 y 1), así que su centroide no es el centro del anillo;
- como referencia: con pasos de ~10° y distancia 1 entre las fotos 1 y 2, el radio implícito es ≈ 1 / (2·sen 4,85°) ≈ 5,9.

### Tiempos (esta PC, solo CPU)

| Etapa | Tiempo |
|---|---|
| `scanner_match` (extracción 12,2 s + pares 18,1 s) | 30,6 s |
| `scanner_reconstruct`: carga | 0,04 s |
| `scanner_reconstruct`: pistas | 0,01 s |
| `scanner_reconstruct`: reconstrucción (12 BA en total) | 0,80 s (BA 0,75 s) |
| `scanner_reconstruct`: color | 0,35 s |
| `scanner_reconstruct`: salidas | 0,01 s |
| `scanner_reconstruct`: total | **1,2 s** |

Con `ba_every = 1` cada BA global tarda entre 23 y 162 ms. Para este tamaño no hace falta espaciarlo.

### Imágenes `--debug` (lo que se ve, con honestidad)

- **`debug_top.jpg`:**
  - Las 11 cámaras forman dos arcos de un mismo anillo (7…1 y 36…33), con el hueco esperado donde faltan las fotos 8–32.
  - Todas tienen el eje óptico apuntando hacia la nube.
  - La nube es alargada, como la planta de una zapatilla vista desde arriba, con franjas que parecen los cordones.
  - Los colores son azul oscuro y gris, coherentes con la zapatilla azul marino.
- **`debug_side.jpg`:**
  - Las cámaras quedan aproximadamente en un plano y la nube desplazada respecto de él, coherente con el celular sostenido por encima de la zapatilla apuntando hacia abajo.
  - El signo de la normal del dibujo es arbitrario.
- **Revisión visual del usuario del `sparse_cloud.ply` en MeshLab o CloudCompare: pendiente (no confirmada).**

## 5. Investigación del resultado parcial

### 5.1 Fallos de algoritmo encontrados con los datos reales (corregidos)

| Problema | Diagnóstico | Corrección |
|---|---|---|
| Solo se registraba el par inicial (2/34) | El 78 % de las pistas del par 1-2 fallaba por `error_alto` (2,5–8 px): la K inicial del EXIF tiene la focal desviada ~6 % y no modela la distorsión. Ninguna otra cámara llegaba a 20 correspondencias 2D–3D | **Retriangulación tras cada BA incremental**: el primer BA refina f, y 939 pistas se recuperan. Prueba `PipelineRecoversFromApproximateInitialIntrinsics`, que falla sin la corrección |
| La retriangulación reponía en la etapa final los puntos de 2 vistas que el filtrado acababa de quitar | Se retriangulaba después de todas las corridas de BA | Solo se retriangula en la fase incremental (probado) |
| Pistas cortadas | SIFT repite un punto con varias orientaciones (mismas coordenadas, otro índice); en la foto 2, 193 de 1556 posiciones aparecían con más de un índice | Las pistas se arman por (foto, posición): de 7588 se pasa a 6719 pistas unidas. Prueba `DuplicateKeypointsAtSamePositionAreJoined` |

### 5.2 Diagnóstico del registro incompleto (variantes medidas, ninguna adoptada)

| Variante | Pares aceptados (Sprint 2) | Cámaras registradas |
|---|---|---|
| Por defecto, antes de las correcciones de 5.1 | 62 | 2 |
| Por defecto, con retriangulación | 62 | 11 |
| + unión de duplicados SIFT (**resultado final**) | 62 | **11** |
| `pnp_min_inliers = 10` (Sprint 3) | 62 | 13 (se corta en la 31 con 9/10 inliers) |
| `ransac_threshold_px = 2` (Sprint 2) | 72 | 12 |
| `min_inliers = 15` (Sprint 2) | 74 | 11 |
| Ventana de vecinos i±1..i±3 con cierre del anillo (5.3) | 62 (mismos 105 pares) | 11 |
| **COLMAP 4.2.1** (automático, calidad media, una cámara, mismas 34 fotos) | emparejamiento exhaustivo propio | **31** (todas menos 9, 10 y 11); 3091 puntos; error medio 0,97 px |

Las variantes `pnp_min_inliers`, `ransac_threshold_px = 2` y `min_inliers = 15` se midieron antes de unir los duplicados. Después de esa corrección, el resultado por defecto no cambió (11).

**Conclusión:**
- Los datos son reconstruibles: COLMAP registra 31 de 34 y su focal coincide con la nuestra.
- Nuestro registro se corta porque el grafo del Sprint 2 es delgado. Muchos eslabones tienen un solo par aceptado y casi no hay pistas de 3 vistas, así que cada cámara nueva ve muy pocos puntos 3D ya triangulados.
- Ningún parámetro del Sprint 2 o del Sprint 3 lo resuelve por sí solo.

### 5.3 Cambio autorizado en `scanner_match`: ventana de vecinos por índice

**Motivo:** probar si emparejar más pares vecinos conectaba el anillo.

**Autorización del usuario (acotada):**
- se permitió ampliar solo la selección de pares: ventana configurable i±1, i±2, i±3 y cierre del anillo;
- **sin tocar** el ratio test, `ransac_threshold_px`, `min_inliers` ni los umbrales de calidad;
- dejando el comportamiento anterior como opción.

**Cambio:** dos claves nuevas en `matching`:
- `neighbor_index_window` (0–10): cada vista se empareja con las N siguientes según su posición entre las fotos usadas;
- `ring_closure`: las últimas vistas también se emparejan con las primeras.

Los pares de la ventana se **unen** a los de orientación (HU-10). Detalle en `docs/formatos.md`.

**Resultado con `window = 3`, `closure = true` (antes / después):**

| | Antes (Sprint 2: `0` / `false`) | Después (`3` / `true`) |
|---|---|---|
| Pares seleccionados | 105 | 105 (**ninguno nuevo**) |
| Pares aceptados | 62 | 62 |
| Cámaras registradas | 11 | 11 |
| Puntos finales | 734 | 734 |
| RMS final | 0,497 px | 0,497 px |
| f final | 3079,3 px | 3079,3 px |

**Por qué no cambió nada:** con pasos de ~10°, las vistas a distancia 3 están a ~30°. La selección por orientación (≤ 45°, hasta 6 vecinas) ya incluía los 102 pares de distancia 1, 2 y 3, con el cierre del anillo, más 3 de distancia 4. El cuello de botella no es qué pares se prueban, sino cuántos pasan la validación: 43 de 105 rechazados, 42 por `pocos_inliers`.

**Criterio de salida acordado:** no se llegó a 20 cámaras, así que se volvió a los valores por defecto anteriores (`neighbor_index_window: 0`, `ring_closure: false`). La ventana queda solo como opción y la densificación del emparejamiento pasa al Sprint 4.

## 6. Parámetros y su comportamiento

| Parámetro | Valor | Comportamiento en `tenis` |
|---|---|---|
| `exclude_rejected_photos` | true | Excluyó las fotos 13 y 19 |
| `init_min_inliers` | 100 | Se eligió el primer candidato (1-2, 1474 inliers) |
| `init_min_median_triangulation_angle_deg` | 5 | El par 1-2 tiene 6,04°, justo por encima: los pasos de ~10° dan ángulos bajos |
| `tri_min_angle_deg` | 1,5 | Sin efecto notable |
| `max_reproj_error_px` | 2 | Con la K del EXIF rechazaba el 78 % de las pistas iniciales; la retriangulación tras el BA lo resolvió. En los dos filtrados quitó 10 observaciones (9 + 1) |
| `pnp_ransac_threshold_px` / `pnp_confidence` | 3 / 0,999 | Entre 78 % (foto 3: 200/256) y 98 % (foto 7: 52/53) de inliers en las cámaras registradas |
| `pnp_min_inliers` | 20 | Es el umbral que detiene el registro (la 8 tenía 10 correspondencias y la 32, 12). Bajarlo a 10 solo suma 2 cámaras |
| `min_views` | 3 | Elimina el 77 % de los puntos (2390 de 3124 eran de 2 vistas) |
| `ba_every` / `ba_max_iterations` | 1 / 100 | 12 corridas en 0,75 s; convergieron todas (máximo 60 iteraciones) |
| `huber_delta_px` | 1 | Sin efecto observable |
| `refine_focal` / `refine_distortion` | true / true | f +6,6 %, coherente con COLMAP |

## 7. Problemas encontrados y cómo se resolvieron

| Problema | Solución |
|---|---|
| Grafo de vistas no conexo en `tenis` (paso 0) | Se informó al usuario, que decidió seguir con el componente de 30 vistas |
| El Ceres de vcpkg no tiene biblioteca dispersa (`CERES_NO_SUITESPARSE`, sin `EIGEN_SPARSE`) | Se elige el resolvedor en tiempo de ejecución: `DENSE_SCHUR`, que alcanza con ~30 cámaras |
| `scanner_server.exe` bloqueado (el usuario lo tenía corriendo para recibir la sesión) | Se compilan solo `scanner_tests`, `scanner_match` y `scanner_reconstruct`; el servidor no se detuvo |
| Un script de PowerShell sin BOM estropeó los acentos de una edición | Se corrigió el texto y se editó con las herramientas de edición |
| Prueba de pose relativa con error de 1,7° | La escena sintética era pequeña (centro de la imagen); se agrandó para llenar la imagen como una foto real. Las tolerancias del documento (0,5° y 1°) no cambiaron |
| Borde del frustum: −0,5 se proyectaba como −0,50000000000001 | Tolerancia de 1e-9 px en la comparación del borde |
| La prueba de la IMU esperaba ~3° de diferencia para una desviación de 3° | Solo se compara la magnitud de la rotación relativa; una desviación perpendicular cambia esa magnitud mucho menos. Se corrigió la expectativa de la prueba, no el código |
| Fallos de algoritmo con datos reales | Sección 5.1 |

## 8. Riesgos para el Sprint 4

- **Reconstrucción parcial** (11 de 34 cámaras, 734 puntos). La densificación y el mallado del Sprint 4 sobre estas poses cubrirían solo un tercio del objeto. Opciones para decidir en el Sprint 4:
  - **densificar el emparejamiento del Sprint 2:** más puntos y correspondencias por par, emparejamiento guiado por F, cruce de pares a más distancia, o emparejamiento exhaustivo como COLMAP;
  - **recapturar** evitando el contraluz de la ventana y sin fotos borrosas;
  - usar el SfM de COLMAP como respaldo para la parte densa.
- **K del EXIF desviada un ~6,6 %.** El BA la corrige, pero el par inicial depende de ella. Se mitigó con la retriangulación.
- **Escala arbitraria** (distancia del par inicial = 1). La escala métrica es la HU-22.
- **Formato de exportación a COLMAP:** `reconstruction.json` ya usa sus convenciones (cuaternión `[w,x,y,z]`, mundo→cámara) y trae las observaciones con el índice del punto en el `.feat`.
- **Ceres sin biblioteca dispersa:** con muchas más cámaras, `DENSE_SCHUR` puede volverse lento. Con 11 cámaras, 12 corridas tardaron 0,75 s.

## 9. Datos objetivos para la Review y la Retrospective

**Commits del sprint (rama `sprint-3/epic-3-reconstruccion-dispersa`):**

| Commit | Contenido |
|---|---|
| `chore(server)` | sección `reconstruction` de la configuración |
| `feat(server)` HU-14 | DLT y modelo de cámara |
| `feat(server)` HU-13 | par inicial y pose relativa |
| `feat(server)` | pistas multivista |
| `feat(server)` HU-15 | registro con PnP-RANSAC |
| `feat(server)` HU-16 | bundle adjustment |
| `feat(server)` HU-17 | filtrado |
| `feat(server)` HU-18 | error por cámara |
| `feat(server)` | `reconstruction.json` y PLY |
| `feat(server)` | `scanner_reconstruct` |
| `fix(server)` | retriangulación y duplicados SIFT |
| `feat(server)` | ventana de vecinos (opción) |
| `docs` | formatos y reporte |

**Datos:**
- Velocidad: 20 de 20 puntos.
- Pruebas: de 107 a 176.
- Resultado real: 11 de 34 cámaras, 734 puntos, RMS 0,50 px, f 3079 px; COLMAP registra 31 de 34 con f 3093 px.
- Paradas para consultar al usuario: 3 (grafo no conexo; cierre con resultado parcial; cambio acotado en `scanner_match`).
- Tiempo de procesamiento: 30,6 s de emparejamiento y 1,2 s de reconstrucción, solo CPU.
- La app móvil y el contrato HTTP no cambiaron. El comportamiento por defecto de `scanner_match` no cambió: la ventana nueva viene desactivada.

### Opiniones del equipo

*(Para completar por el equipo en la Review y la Retrospective.)*

## Corrección posterior (v1.0.1): búsqueda de focal del par inicial

Es una **corrección posterior** al cierre del Sprint 4 (`v1.0`), autorizada por el usuario el 28/11/2026 y encontrada con la sesión `20261128-115016-llamav2` (41 fotos de una llama, el mismo Poco X3 Pro). Se publicó como `v1.0.1`.

### Causa

- El emparejamiento de llamav2 era muy bueno:
  - 41 de 41 fotos aceptadas;
  - 119 de 126 pares aceptados (los 7 rechazados, por `razon_inliers_baja`);
  - grafo de vistas **conexo**.
- Pero la reconstrucción registraba **2 de 41 cámaras**:
  - el par inicial 9-10 (1314 inliers, ángulo mediano 7,1°, 100 % de los puntos delante) **no triangulaba ningún punto**;
  - todas sus pistas fallaban los controles de HU-14 por `error_alto`, con errores de 8–9 px y límite de 2 px;
  - sin puntos 3D, ninguna otra cámara se podía registrar con PnP, y la densificación terminaba con 0 puntos.
- El motivo es que la K inicial (focal del EXIF: 2888,8 px, sin distorsión) estaba demasiado lejos de la real:
  - el bundle adjustment converge a f = 3216 px, **+11,3 %**, y k1 = −0,115;
  - la retriangulación que se agregó en el Sprint 3 nunca llegaba a actuar, porque requiere al menos un primer BA con puntos.
- En `tenis` (f real +6,6 %) sí pasaban algunos puntos, por eso no se notó.
- **No es un problema de patrones repetidos:** el emparejamiento de llamav2 era bueno.

### Corrección

- Primero se triangula el par inicial con la K actual (factor 1,0), exactamente como antes.
- **Solo si deja 0 puntos**, se prueba una rejilla de factores de focal para ese par:
  - `init_focal_factor_min` 0,8, `init_focal_factor_max` 1,4, `init_focal_factor_step` 0,05;
  - E se convierte con `F = K₀⁻ᵀ E K₀⁻¹` y `E' = K'ᵀ F K'`.
- Se elige el factor con más puntos dentro de los mismos controles; en caso de empate, el de menor error mediano.
- Después el bundle adjustment refina f como siempre.
- Se activa con `init_focal_search` (true por defecto).
- **No cambia `max_reproj_error_px` ni ningún otro umbral.**
- La rejilla probada queda en `reconstruction_report.json` (`initial_pair.focal_search`).

Curva real de llamav2, par 9-10 (puntos que pasan los controles):

| Factor | 0,80 | 0,85–1,00 | 1,05 | 1,10 | **1,15** | 1,20 | 1,25 | 1,30 | 1,35 | 1,40 |
|---|---|---|---|---|---|---|---|---|---|---|
| Puntos | 3 | 0 | 1 | 231 | **489** | 373 | 279 | 270 | 232 | 202 |

**Pruebas** (5 nuevas, 245 en total, todas verdes):
- Configuración.
- Par real 9-10 de llamav2 como dato de prueba (`server/tests/data/llamav2_par_009_010.*`):
  - sin la búsqueda, 0 puntos;
  - con ella, factor entre 1,10 y 1,20 y más de 300 puntos;
  - con la rejilla acotada a 0,9–1,0, sigue en 0.
- Escena sintética con K correcta: la rejilla no se activa y el resultado es el mismo.
- **Escena sintética de focal errónea:** se intentó una de ±15 %, como estaba pedido, pero en una escena pinhole ideal un error de focal de ±15 % (y hasta ±30 %, con distorsión o con pasos de 10°) **no** deja al par sin puntos, porque con F exacta la pose de dos vistas lo absorbe. Por eso el caso que reproduce el fallo usa los datos reales.

### Antes y después (`scripts\run-pipeline.ps1`, mismos parámetros salvo la corrección)

| | llamav2 antes | llamav2 después | tenis antes | tenis después |
|---|---|---|---|---|
| Rejilla de focal | — | activada: factor 1,15 (f 3322 px), 489 puntos | — | **no activada** (factor 1,0) |
| Cámaras registradas | **2 de 41** | **41 de 41** | 11 de 34 | 11 de 34 |
| Puntos dispersos finales | 0 | 6628 | 734 | 734 |
| RMS final | — | 0,726 px | 0,497 px | 0,497 px |
| f final | 2888,8 (sin BA útil) | 3216,0 px | 3079,3 px | 3079,3 px |
| Pipeline | se detenía antes de densificar (guardia de `run-pipeline.ps1`) | completo en 2512 s | completo | completo en 86,6 s |
