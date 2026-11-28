# Reporte del Sprint 4 (Epic 4): malla texturizada, exportación y precisión

**Meta:** a partir de la reconstrucción refinada, el sistema genera un modelo OBJ texturizado (con escala opcional), lo muestra en varias vistas y emite un reporte de precisión comparado con COLMAP.

Rama `sprint-4/epic-4-malla-texturizada`, integrada en `main` con las etiquetas `v0.4-sprint4` y `v1.0`.
Todos los números salen de ejecuciones reales en la PC del equipo:
- CPU, 15,6 GB de RAM y GPU NVIDIA RTX 3050 Laptop de 6 GB;
- COLMAP 4.2.1 con CUDA;
- sesión `20261121-142259-tenis`, elegida por el usuario.

## 1. Historias y puntos

| ID | Historia | Pts | Estado |
|---|---|---|---|
| HU-19 | Como operador quiero densificar la reconstrucción con COLMAP usando mis poses refinadas. | 5 | Hecha con pruebas verdes |
| HU-20 | Como operador quiero generar la malla triangulada desde la nube densa. | 3 | Hecha con pruebas verdes |
| HU-21 | Como operador quiero asignar a cada triángulo la textura de su mejor cámara. | 5 | Hecha con pruebas verdes |
| HU-22 | Como usuario quiero exportar OBJ, MTL y textura (con escala opcional por distancia de referencia) listos para impresión 3D. | 2 | Hecha con pruebas verdes |
| HU-23 | Como operador quiero ver la nube y la malla en una vista general y una de detalle. | 3 | Hecha con pruebas verdes |
| HU-24 | Como operador quiero un reporte de precisión comparado con la reconstrucción de COLMAP. | 2 | Hecha con pruebas verdes |

**Puntos completados: 20 de 20.**

El modelo de la sesión real **cubre solo la parte del objeto que vieron las 11 cámaras registradas** en el Sprint 3: un arco de unos 115° del anillo, el frente y un costado de la zapatilla. La sección 4.6 lo detalla.

### Criterios de aceptación y evidencia

| HU | Criterio (resumen) | Evidencia |
|---|---|---|
| HU-19 | Nube densa con color y normales, con más puntos que la dispersa | `ColmapIo.*`, `ColmapRunner.*` (con un ejecutable falso), `Dense.*`. En `tenis`: **113 190 puntos densos frente a 734 dispersos** |
| HU-20 | Malla con vértices y caras, sin triángulos degenerados ni zonas de baja densidad | `Meshing.*`: limpieza de degenerados y componentes sueltas, cubo cerrado y plano abierto. El recorte de baja densidad es el `trim` de Poisson. En `tenis`: 22 606 vértices y 43 361 caras, 0 degeneradas |
| HU-21 | Cada triángulo recibe la imagen de la cámara que mejor lo ve, sin oclusión; atlas con UV en [0, 1] | `Texturing.*`: cara de frente, oclusión, cara de canto sin cámara, UV en rango, color del atlas igual al de la foto, empaquetado sin solapes, reducción uniforme. En `tenis`: **76,1 %** de las caras con textura |
| HU-22 | OBJ, MTL y textura; con distancia de referencia, el modelo queda a escala | `ObjExport.*`: índices base 1 en rango, MTL relativo, distancia escalada = D (error relativo < 1e-6), factor directo, sin escala, puntos iguales rechazados, ida y vuelta |
| HU-23 | Vista general y de detalle del mismo modelo, con rotación y zoom | `Viewer.*`: órbita y proyección a mano, z-buffer, cubo texturizado, tamaño de la salida doble, decimación. Ventana interactiva y `--render` (imágenes de la sección 4.5) |
| HU-24 | Error de posición y rotación de las cámaras comunes tras alinear por similitud, diferencia de f y reproyección de ambos | `Accuracy.*`: Umeyama exacto (< 1e-6), con ruido, RANSAC con atípicos, menos de 4 comunes = no fiable, estructura del JSON, comandos de la referencia. Resultados reales en la sección 4.4 |

## 2. Pruebas

| | Cantidad |
|---|---|
| Pruebas de los Sprints 1 a 3 (siguen verdes) | 176 |
| Pruebas nuevas del Sprint 4 | 64 |
| **Total (`ctest`)** | **240, todas verdes** |

Pruebas nuevas por grupo:

| Grupo | Pruebas | Grupo | Pruebas |
|---|---|---|---|
| T0b: `GuidedMatching`, `MutualCheck`, contraste SIFT, config | 7 | `Meshing` (HU-20) | 5 |
| `PipelineConfig` | 3 | `Texturing` (HU-21) | 8 |
| `ColmapIo` (HU-19) | 5 | `ObjExport` (HU-22) | 8 |
| `ColmapRunner` (HU-19) | 4 | `Viewer` (HU-23) | 8 |
| `Dense` (HU-19) | 4 | `Accuracy` (HU-24) | 8 |
| `SessionPipeline` | 4 | | |

Todas usan datos sintéticos (cubos, planos, anillos de cámaras e imágenes generadas). La ejecución de COLMAP se prueba con `colmap_stub`, un ejecutable falso que registra los argumentos y crea las salidas esperadas. Ninguna prueba depende de COLMAP, de la GPU ni de fotos reales.

## 3. T0: sesión de referencia y cobertura

1. **Sesión:** el usuario indicó `tenis` (no podía subir otra): 36 fotos, 34 aceptadas por calidad.
2. **Valores por defecto:** 62 pares aceptados, grafo con 2 componentes (30 y 4 vistas), **11 de 34 cámaras registradas (32 %)**, 734 puntos, RMS 0,497 px, f 3079 px.
3. **Puerta de decisión:** 32 % < 70 %, así que se pasó a T0b.

### T0b: mejora del emparejamiento (dos rondas, opciones nuevas apagadas por defecto)

| Variante (`scanner_match`; `scanner_reconstruct` con sus valores por defecto) | Pares aceptados | Cámaras | Puntos finales | RMS final | f |
|---|---|---|---|---|---|
| Por defecto (Sprint 2/3) | 62 | **11** | 734 | 0,497 px | 3079,3 px |
| Ronda 1a: `sift_contrast_threshold` 0,02 | 67 | 11 | 854 | 0,551 px | 3089,0 px |
| Ronda 1b: 1a + `guided_matching` | 67 | 2 | 0 | — | — |
| Ronda 2: 1b + `mutual_check` | 71 | 2 | 0 | — | — |

**Diagnóstico:**
- El contraste bajo detecta más puntos (mediana de 2888 a 5481 por foto) y acepta 5 pares más, pero no conecta el anillo.
- El emparejamiento guiado multiplica los inliers de los pares aceptados (mediana de 88 a 533), pero la reconstrucción colapsa:
  - 1300 pistas inconsistentes (55 antes) y, del par inicial, solo 5 puntos triangulables;
  - la verificación cruzada baja las inconsistentes a 381, pero el par inicial deja 1 punto;
  - hipótesis, no verificada porque las rondas permitidas se acabaron: dentro de una banda de 1 px, muchas correspondencias falsas se deslizan a lo largo de la línea epipolar; cumplen F pero triangulan detrás de la cámara o a profundidades absurdas.
- Ninguna ronda mejoró al menos 5 cámaras, así que se volvió a los valores por defecto. Las tres opciones quedan en la configuración, apagadas (con 0,04 / false / false el resultado es el del Sprint 2; probado).

**Decisión del usuario:**
- La ruta oficial de HU-19 usa **nuestras poses (11 cámaras)**.
- Como **comparación documentada**, se repite la densificación, la malla y la textura con las poses de COLMAP (30 cámaras), con `--poses colmap`, en `dense_colmap\` y `model_colmap\`. No es la entrega de HU-19: es un desvío explícito para tener el modelo completo como referencia.

## 4. Resultados con la sesión real

### 4.1 Ejecución de punta a punta (`scripts\run-pipeline.ps1 -Session sessions\20261121-142259-tenis`)

| Etapa | Segundos | Resultado |
|---|---|---|
| Emparejamiento (Sprint 2) | 30,3 | ok: 62 pares aceptados de 105 |
| Reconstrucción dispersa (Sprint 3) | 1,3 | ok: 11 de 34 cámaras, 734 puntos |
| Densificación y malla (HU-19, HU-20) | 9,3 (*) | ok |
| Textura y OBJ (HU-21, HU-22) | 3,6 | ok |
| Precisión frente a COLMAP (HU-24), con la referencia | 34,2 | ok |
| **Total** | **78,7** | |

(*) En esta ejecución COLMAP **reutilizó los mapas de profundidad** que ya estaban en `dense\` de la corrida anterior, con las mismas poses (`patch_match_stereo` tardó 0,2 s). El costo real desde cero, medido en la primera corrida, fue:
- `image_undistorter`: 2,0 s;
- `patch_match_stereo`: **455,7 s**;
- `stereo_fusion`: 4,8 s;
- total de `scanner_dense`: 465,7 s.

Con 11 imágenes a 1600 px, `patch_match_stereo` usó ~1,4 GB de memoria de GPU (`nvidia-smi` durante la corrida; COLMAP no la informa).

### 4.2 Nube, malla y textura (nuestras poses, ruta oficial)

| | Valor |
|---|---|
| Cámaras registradas usadas en la densificación | 11 (1–7 y 33–36) |
| Puntos dispersos / densos | 734 / **113 172** |
| Malla de Poisson (`depth` 9, `trim` 7) | 22 741 vértices, 43 603 caras |
| Limpieza | 0 caras degeneradas; 7 componentes de menos de 100 caras (242 caras); 135 vértices sueltos |
| **Malla final** | **22 606 vértices, 43 361 caras**, 17 componentes, **abierta** (1833 aristas abiertas), área 226,8 (unidades de la reconstrucción) |
| Textura | **76,1 %** de las caras (32 982 de 43 361); 3266 cartas; atlas de 4095×3730; cartas reducidas a 0,69 para caber en 4096 |
| Caras por cámara | 1: 3824 · 2: 2004 · 3: 1655 · 4: 1192 · 5: 1218 · 6: 1588 · 7: 9686 · 33: 5687 · 34: 3194 · 35: 1057 · 36: 1877 |
| Exportación | `model\model.obj`, `model.mtl`, `texture.png` (atlas); escala **arbitraria** (factor 1): no se dio `--scale-ref` porque no hay una medida real del objeto |

### 4.3 Comparación con las poses de COLMAP (desvío documentado)

| | Nuestras poses | Poses de COLMAP |
|---|---|---|
| Cámaras | 11 | 30 |
| Puntos dispersos / densos | 734 / 113 172 | 5675 (referencia de esa corrida; la regenerada en la ejecución final tiene 5838) / 263 257 |
| `patch_match_stereo` | 455,7 s | 573,9 s |
| Malla final | 22 606 v, 43 361 caras, 17 componentes, 1833 aristas abiertas | 24 061 v, 47 415 caras, 60 componentes, 503 aristas abiertas |
| Caras con textura | 76,1 % | 50,8 % |
| Atlas | 4095×3730, cartas al 0,69 | 4093×3985, cartas al 0,52 |

La primera corrida con las poses de COLMAP la detuvo Claude Code porque **el sistema se quedó sin memoria** (iba en 38 de ~60 mapas de profundidad). Por indicación del usuario se repitió con el mismo `max_image_size` 1600, después de cerrar otros programas, y terminó bien. No hizo falta bajar a 1200.

Con 30 cámaras, COLMAP también reconstruye **mucho del entorno** (piso, muebles, ventana): la malla tiene 60 componentes.

Ese entorno explica en parte el porcentaje de textura más bajo: son superficies vistas de canto (cos(θ) < 0,2) o tapadas. Es una lectura de las imágenes de la sección 4.5, no una medición separada.

### 4.4 Precisión frente a COLMAP (HU-24, `accuracy\accuracy_report.json`)

**Referencia de COLMAP:**
- mismas 34 fotos aceptadas, `single_camera` y modelo `RADIAL`;
- registró **30 de 34** imágenes, con 5838 puntos, en 33,6 s.

**Alineación:** similitud de Umeyama con RANSAC (más de 8 cámaras comunes); 11 de 11 inliers.

| Medida | Valor |
|---|---|
| Cámaras: propias / COLMAP / comunes | 11 / 30 / **11** (fiable: ≥ 4) |
| Factor de escala propio → COLMAP | 0,2995 |
| Radio medio del anillo (las 30 cámaras de COLMAP) | 3,576 |
| **Error de posición de las cámaras** (medio / RMS / máximo) | 0,0048 / 0,0054 / 0,0105, es decir **0,14 % / 0,15 % / 0,29 % del radio** |
| **Error de rotación de las cámaras** (medio / máximo) | **0,28° / 0,38°** |
| **f** propia / COLMAP | 3079,3 / 3084,8 px (**−0,18 %**) |
| k1, k2 propios / COLMAP | 0,105, −0,280 / 0,075, −0,162 |
| **Error de reproyección** propio (medio / RMS) | **0,41 / 0,50 px** |
| **Error de reproyección** de COLMAP (medio / RMS) | **0,67 / 0,88 px** |
| Puntos dispersos propios → punto de COLMAP más cercano | media 0,0112, mediana 0,0091 (0,26 % del radio) |

**Lectura:**
- En las cámaras que sí registramos, las poses coinciden con las de COLMAP con errores de centésimas de grado y décimas de porcentaje.
- La diferencia con COLMAP está en la **cobertura** (11 frente a 30 cámaras), no en la precisión.
- k1 y k2 difieren más que f. Con una sola parte del anillo, la distorsión está menos determinada; aun así, ambos modelos reproyectan por debajo de 1 px.
- El error de reproyección de cada uno se mide sobre sus propios puntos. El nuestro es menor en parte porque el filtrado de HU-17 se queda solo con puntos de 3 o más vistas y error ≤ 2 px.

### 4.5 Imágenes del visor (`scanner_view --render`, en `docs\img\`)

| Archivo | Contenido |
|---|---|
| `tenis_nube_dispersa.jpg` | Nube dispersa (734 puntos) y las 11 cámaras |
| `tenis_nube_densa.jpg` | Nube densa (vistas general y de detalle) |
| `tenis_malla_texturizada.jpg` | Malla texturizada con nuestras poses |
| `tenis_malla_poses_colmap.jpg` | Malla texturizada con las poses de COLMAP (comparación) |

Lo que se ve, con honestidad:
- **Nube densa:** la zapatilla se reconoce. En la vista de detalle se ven la malla azul del capellada, el contorno de los cordones y la suela blanca. Alrededor hay ruido: manchas blancas del piso claro y de la luz de la ventana.
- **Malla texturizada (nuestras poses):**
  - en el centro, la zona de la zapatilla tiene la textura correcta (tejido azul, suela blanca);
  - el Poisson también cierra superficie en el fondo, que queda como **fragmentos sueltos** (17 componentes);
  - las zonas grises son caras sin cámara válida (23,9 %).
- **Malla con poses de COLMAP:**
  - el encuadre automático (percentil 95) incluye fragmentos lejanos del entorno, así que la zapatilla queda pequeña dentro del anillo de cámaras (magenta) y el detalle automático cae sobre un fragmento del fondo;
  - esta imagen **no es representativa del objeto**; para mirarlo conviene la ventana interactiva (clic sobre la zapatilla) o MeshLab.

### 4.6 Cobertura del objeto

- **Reconstruido (ruta oficial):**
  - las caras de la zapatilla visibles desde las fotos 1–7 y 33–36, un arco de unos 115° del anillo;
  - es decir, el frente y un costado: capellada, puntera, parte de los cordones y el borde de la suela de ese lado.
- **No reconstruido:**
  - el lado opuesto y el talón, que se ven desde las fotos 8–32;
  - el sector de las fotos 9–12 se perdió ya en el Sprint 2 por el contraluz de la ventana, y las 14–32 por el grafo delgado del emparejamiento;
  - la planta, que no la ve ninguna cámara.
- **Comparación con 30 cámaras de COLMAP:** cubre casi todo el contorno (salvo el sector 9–11) y también más del entorno.

## 5. Parámetros y su comportamiento

| Sección | Valor | Comportamiento en `tenis` |
|---|---|---|
| `dense.max_image_size` | 1600 | Cupo en 6 GB de GPU (~1,4 GB con 11 imágenes). La corrida de 30 imágenes falló una vez por falta de **RAM del sistema** (no de GPU) y anduvo al repetirla con menos programas abiertos |
| `dense.geom_consistency` | true | Fusión geométrica; nube con color y normales |
| `mesh.poisson_depth` / `trim_density` | 9 / 7 | ~43 600 caras; el `trim` quita lo más lejano, pero quedan fragmentos del fondo |
| `mesh.min_component_faces` | 100 | Eliminó 7 componentes (242 caras); quedan 17 |
| `texture.min_cos` | 0,2 | 76,1 % de caras con textura; el resto queda gris |
| `texture.atlas_max_size` | 4096 | Las cartas se reducen al 0,69 (al 0,52 con 30 cámaras) |
| `texture.depth_map_long_side` / `padding_px` | 1000 / 2 | Sin sangrado visible en las imágenes del visor |
| `viewer.*` | 1280×640, 150 000 caras al mover | La malla completa (43 361 caras) se dibuja sin decimar |
| `accuracy.min_common_cameras` / `ransac_iterations` | 4 / 200 | 11 comunes; RANSAC dejó las 11 como inliers |

## 6. Problemas encontrados y cómo se resolvieron

| Problema | Solución |
|---|---|
| T0: la puerta del 70 % de cámaras no se cumplía (11 de 34) | Dos rondas de T0b sin mejora. Se volvió a los valores por defecto y el usuario decidió la ruta (sección 3) |
| `model_converter` de COLMAP 4.2.1 terminaba sin mensaje (código −1073740791) | Necesita que la carpeta de salida exista; se crea antes (también ocurrió en el Sprint 3) |
| Medio píxel de diferencia entre la convención propia y la de COLMAP | +0,5 al exportar y −0,5 al leer, con pruebas de ida y vuelta |
| "Radio del anillo" calculado con las cámaras comunes (un arco): el porcentaje de error salía inflado (0,30 %) | Se mide con todas las cámaras de COLMAP (0,14 %). Antes de corregirlo se vio el valor y se dejó documentado |
| La comparación con las poses de COLMAP se detuvo por falta de memoria del sistema | Se repitió con el mismo `max_image_size`, después de cerrar otros programas (indicación del usuario) |
| OpenCV de vcpkg sin `highgui` (lo necesita la ventana de `scanner_view`) | Se agregaron las *features* `highgui` y `win32ui` y se recompiló con `VCPKG_MAX_CONCURRENCY=2` (~8 min), sin otra tarea pesada en paralelo |
| El servidor del usuario tenía abiertas las DLL viejas de OpenCV en `build\Release` | Con permiso del usuario se detuvo el servidor para enlazar las pruebas y las herramientas |
| La prueba del ejecutor de COLMAP dejaba `args.txt` abierto al borrar la carpeta (bloqueo de Windows) | Se cierra el archivo antes de borrar |
| Mensaje de commit con BOM (`Out-File` de PowerShell 5.1) | Se corrigió con `--amend` antes de publicar; desde entonces los mensajes se escriben sin BOM |

## 7. Limitaciones y mejoras futuras

- **Cobertura parcial con nuestras poses:**
  - el emparejamiento del Sprint 2 (vecinos por orientación, ratio 0,75, Sampson 1 px, mínimo 30 inliers) deja un grafo delgado y el SfM registra 11 de 34 cámaras;
  - COLMAP, con emparejamiento exhaustivo, registra 30;
  - **mejoras posibles:**
    - emparejamiento exhaustivo o de más vecinos con verificación geométrica;
    - corregir el emparejamiento guiado (umbral más estricto en la banda, o exigir consistencia en 3 vistas);
    - recapturar sin contraluz.
- **Malla con fragmentos del entorno:**
  - Poisson cierra superficie también en el fondo;
  - mejoras: recortar por caja o por distancia a las cámaras, o segmentar el objeto (por ejemplo, la componente más grande cerca del centro del anillo).
- **Malla abierta** (1833 aristas abiertas): no está lista para impresión 3D sin cerrarla (por ejemplo en MeshLab); HU-22 solo informa.
- **Textura sin mezcla entre cartas ni corrección de costuras:** cada triángulo toma una sola foto; se notan cambios de brillo entre cartas.
- **Escala arbitraria:** no se tomó una medida real del objeto (sección 8).
- **Visor:**
  - el encuadre automático usa el percentil 95 de todo el modelo; con mucho entorno, el objeto queda pequeño;
  - el detalle automático de `--render` puede caer en el fondo; en la ventana se elige con clic.
- **Memoria:** densificar 30 imágenes a 1600 px con otros programas abiertos agotó la RAM una vez.

## 8. Verificación visual del usuario (hecha)

La sección 5.2 del documento del sprint pide al usuario que abra `sessions\20261121-142259-tenis\model\model.obj` en MeshLab, con su textura, y diga:
- si reconoce el objeto;
- si la textura está bien puesta;
- si las proporciones son razonables.

**Confirmada por el usuario.** Su respuesta, textual:

> "ya lo revisé, se reconstruye de forma muy acertada el tenis, solo flaquea mucho por la parte de atrás, pero eso era evidente debido a la luz que tiene la imagen, haré más pruebas más tarde, de momento el trabajo está bien"

- **Lo que confirma:** se reconoce el objeto y la reconstrucción del frente es acertada.
- **La parte de atrás es débil:** coincide con la cobertura de la sección 4.6 (las fotos del lado opuesto no se registraron, en parte por el contraluz).
- **No comentó por separado** la textura ni las proporciones.
- **Escala:** no se usó `--scale-ref`, así que no hay comprobación con una medida real.

## 9. Datos objetivos para la Review y la Retrospective

**Commits del sprint (rama `sprint-4/epic-4-malla-texturizada`):**

| Commit | Contenido |
|---|---|
| `feat(server)` T0b | opciones de emparejamiento |
| `chore(server)` | configuración |
| `feat(server)` HU-19 | exportación COLMAP y densificación |
| `feat(server)` HU-20 | malla y `scanner_dense` |
| `feat(server)` HU-21 | textura |
| `feat(server)` HU-22 | OBJ |
| `feat(server)` HU-24 | precisión |
| `feat(server)` | `scanner_texture` y `scanner_accuracy` |
| `feat(server)` HU-23 | renderizador |
| `feat(server)` HU-23 | `scanner_view` |
| `chore` | `run-pipeline.ps1` |
| `docs` | cierre |

**Datos:**
- Velocidad: 20 de 20 puntos.
- Pruebas: de 176 a 240.
- Resultado real: modelo texturizado del frente de la zapatilla (11 cámaras), con poses a 0,14 % del radio y 0,28° de las de COLMAP, f a −0,18 %.
- Comparación con 30 cámaras de COLMAP: modelo más completo, con más entorno.
- Tiempos: pipeline completo en 78,7 s reutilizando los mapas de profundidad; ~8 min con la densificación desde cero, casi todo en `patch_match_stereo` en GPU.
- Paradas para consultar al usuario:
  - sesión;
  - ruta tras T0b;
  - memoria;
  - recompilación de OpenCV;
  - detener el servidor.
- La app móvil y el contrato HTTP no cambiaron. El comportamiento por defecto de `scanner_match` y `scanner_reconstruct` no cambió: las opciones de T0b vienen apagadas.

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

**Resultado completo de llamav2 después de la corrección:**

| Etapa | Resultado |
|---|---|
| Emparejamiento | 119 de 126 pares, 102,8 s |
| Nube densa | 616 004 puntos (`patch_match_stereo`: 2149 s con 41 imágenes a 1600 px) |
| Malla | 21 757 vértices y 42 905 caras, 12 componentes, abierta (581 aristas abiertas) |
| Textura | **79,9 %** de las caras; atlas de 4096×3623, cartas al 0,40 |
| Precisión frente a COLMAP | COLMAP registra 41 de 41; las 41 son comunes |

Detalle de la precisión frente a COLMAP:
- posición: media 1,29 %, RMS 1,52 % y máximo 3,29 % del radio del anillo;
- rotación: media 0,93° y máximo 1,74°;
- f: 3216 frente a 3125 px (+2,9 %);
- reproyección: RMS 0,73 px propia frente a 1,49 px de COLMAP.

Con el anillo completo, las diferencias frente a COLMAP son mayores que en el arco de 11 cámaras de `tenis`, pero siguen por debajo del 2 % del radio y de 2°.

**`tenis` después de la corrección:**
- la parte dispersa es idéntica (la rejilla no se activa);
- las etapas con COLMAP varían un poco entre corridas (nube densa de 113 185 frente a 113 172 puntos) porque se regeneran.

También se agregó una guardia en `run-pipeline.ps1`: si la reconstrucción dispersa registra menos de 4 cámaras, el pipeline se detiene antes de densificar (código 4) e indica el motivo.
