# Reporte del Sprint 2 (Epic 2): características y geometría epipolar

**Meta:** para cada par de vistas vecinas de una sesión existente, el servidor entrega correspondencias SIFT validadas con la geometría epipolar (matrices F y E) y descarta los pares inválidos.

Rama `sprint-2/epic-2-geometria-epipolar`, integrada en `main` con la etiqueta `v0.2-sprint2`.
Todos los números de este reporte salen de ejecuciones reales en la PC del equipo:
- las pruebas automáticas;
- `scanner_match` sobre la sesión de referencia `20261120-212147-llama`.

## 1. Historias y puntos

| ID | Historia | Pts | Estado |
|---|---|---|---|
| HU-07 | Como operador quiero extraer características SIFT de cada foto. | 3 | Hecha con pruebas verdes |
| HU-08 | Como operador quiero normalizar el contraste antes de extraer características para tolerar iluminación variable. | 2 | Hecha con pruebas verdes |
| HU-09 | Como operador quiero emparejar descriptores entre pares de fotos con ratio test. | 5 | Hecha con pruebas verdes |
| HU-10 | Como operador quiero emparejar solo vistas vecinas (según la orientación registrada) para reducir el cómputo. | 2 | Hecha con pruebas verdes |
| HU-11 | Como operador quiero estimar F con RANSAC y obtener E con K para validar la geometría de cada par. | 5 | Hecha con pruebas verdes |
| HU-12 | Como operador quiero rechazar pares con desviación epipolar mayor al umbral para descartar falsos emparejamientos. | 3 | Hecha con pruebas verdes |

**Puntos completados: 20 de 20.**

Las historias están hechas en el sentido de sus criterios de aceptación. El **resultado** sobre la sesión de referencia, en cambio, es pobre: solo se aceptan 5 de 44 pares (sección 4). Los criterios BDD no fijan cuántos pares deben aceptarse, pero esto es un riesgo serio para el Sprint 3 (sección 8).

### Criterios de aceptación

| HU | Criterio | Evidencia |
|---|---|---|
| HU-07 | Descriptores de 128 dimensiones; al menos 500 puntos por foto en la sesión de referencia | `Features.*`. En la sesión real hay entre 2179 y 7471 puntos por foto, con mediana 4972 |
| HU-08 | Con brillo y contraste alterados se conserva al menos el 80 % de las correspondencias | `ContrastNormalization.*`. Con CLAHE se conserva el **94,9 %** (2361 de 2487); sin CLAHE, el 75,0 % (1856 de 2475) |
| HU-09 | Ratio test 0,75; correspondencias candidatas con su distancia | `Matching.*`. Con transformación conocida, al menos el 90 % de las correspondencias son correctas; con textura repetida se acepta menos del 20 % |
| HU-10 | Solo vistas a 45° o menos y menos pares que todos contra todos | `Pairing.*`. Anillo sintético: 40 pares de 190, con cierre del anillo. En la sesión real: **44 de 190** |
| HU-11 | F de rango 2; inliers con `x'ᵀFx ≈ 0` dentro del umbral de Sampson; `E = K'ᵀ F K` | `Sampson.*` y `Epipolar.*` (escena sintética; detalle debajo de esta tabla) |
| HU-12 | Menos de 30 inliers o razón menor de 0,25 → rechazado con su motivo y sin pasar al Sprint 3 | `PairValidation.*`, `MatchingReport.*` y `SessionMatchingTest.*`. Los pares rechazados no generan archivo de inliers y la carpeta `matches\` se vacía en cada corrida |

Detalle de HU-11 con la escena sintética:
- con 40 % de correspondencias falsas, RANSAC recupera al menos el 90 % de los inliers verdaderos;
- con 0,3 px de ruido, el error de Sampson medio es menor de 0,5 px;
- sin ruido, E coincide con la verdadera con un error menor de 1e-3.

## 2. Pruebas

| | Cantidad |
|---|---|
| Pruebas del Sprint 1 (siguen verdes) | 50 |
| Pruebas nuevas del Sprint 2 | 57 |
| **Total (`ctest`)** | **107, todas verdes** |

Pruebas nuevas por grupo:

| Grupo | Pruebas | Grupo | Pruebas |
|---|---|---|---|
| `Features` (HU-07) | 8 | `Epipolar` (HU-11) | 7 |
| `ContrastNormalization` (HU-08) | 3 | `PairValidation` (HU-12) | 6 |
| `Matching` (HU-09) | 5 | `MatchingConfig` | 5 |
| `Pairing` (HU-10) | 6 | `MatchingReport` | 5 |
| `Intrinsics` (sección 3.3) | 4 | `Inliers` | 3 |
| `Sampson` (HU-11) | 2 | `SessionMatching` (extremo a extremo con una sesión sintética) | 3 |

Las pruebas unitarias usan datos sintéticos, generados en la propia prueba:
- manchas gaussianas en posiciones conocidas;
- texturas aleatorias deformadas con una transformación conocida;
- mosaicos repetidos;
- escenas 3D con K, R y t conocidos.

Ninguna depende de fotos reales en disco.

## 3. Parámetros usados (valores iniciales, sin cambios)

Están en `server/config.default.json`, sección `matching`:

| Parámetro | Valor |
|---|---|
| `work_long_side` | 2000 |
| `max_features` | 8000 |
| `clahe` | true |
| `clahe_clip_limit` | 2.0 |
| `clahe_tile` | 8 |
| `ratio` | 0,75 |
| `max_angle_deg` | 45 |
| `max_neighbors` | 6 |
| `ransac_threshold_px` | 1,0 |
| `ransac_confidence` | 0,999 |
| `min_inliers` | 30 |
| `min_inlier_ratio` | 0,25 |
| `fallback_focal_factor` | 1,2 |

RANSAC usa como máximo 10 000 iteraciones (`kRansacMaxIters`). El valor por defecto de OpenCV, 1000, no basta con razones de inliers bajas.

Tras ver el resultado de la sección 4, el equipo decidió **mantener los valores iniciales** y documentar su comportamiento (sección 5) en lugar de ajustarlos.

### Matriz K aproximada y su límite

- Todas las fotos de la sesión de referencia traen `focal_35mm = 25`, así que no se usó el respaldo.
- `f = 25 × 5000 / 43,27 = 2888,8 px` y punto principal en (2000, 1500), igual para las 20 fotos (4000×3000).
- Es una **aproximación**:
  - supone que la imagen no está recortada respecto al sensor;
  - supone píxeles cuadrados;
  - no modela sesgo ni distorsión de lente.
- El bundle adjustment del Sprint 3 debe refinarla. F no depende de K; E sí hereda su error.

## 4. Resultados con la sesión de referencia

`scanner_match --session ..\sessions\20261120-212147-llama --debug`, con 20 fotos de 4000×3000 del Poco X3 Pro, todas aceptadas en el Sprint 1.

### Pares

| | Valor |
|---|---|
| Pares posibles (todos contra todos) | 190 |
| Pares seleccionados por orientación (HU-10) | **44** |
| Aceptados | **5** |
| Rechazados | **39** |
| Motivo `pocos_inliers` | 36 pares |
| Motivo `razon_inliers_baja` | 37 pares (34 pares tienen ambos motivos) |
| Motivo `f_no_estimada` | 0 pares |

Pares aceptados:

| Par | Ángulo | Correspondencias | Inliers | Razón | Sampson medio (px) |
|---|---|---|---|---|---|
| 1-2 | 17,4° | 643 | 298 | 0,463 | 0,433 |
| 5-6 | 16,4° | 231 | 58 | 0,251 | 0,420 |
| 10-11 | 11,2° | 330 | 110 | 0,333 | 0,415 |
| 11-12 | 13,8° | 308 | 101 | 0,328 | 0,405 |
| 12-13 | 15,1° | 317 | 97 | 0,306 | 0,392 |

Resumen de los pares aceptados (mínimo / mediana / máximo):

| Medida | Mínimo | Mediana | Máximo |
|---|---|---|---|
| Correspondencias | 231 | 317 | 643 |
| Inliers | 58 | 101 | 298 |
| Razón de inliers | 0,251 | 0,328 | 0,463 |
| Error de Sampson medio | 0,392 px | 0,415 px | 0,433 px |

Rechazados con más inliers, los más cercanos a pasar:

| Par | Ángulo | Inliers / correspondencias | Razón | Motivo |
|---|---|---|---|---|
| 14-15 | 15,9° | 56 / 250 | 0,224 | `razon_inliers_baja` |
| 9-10 | 14,3° | 51 / 225 | 0,227 | `razon_inliers_baja` |
| 13-14 | 16,0° | 46 / 281 | 0,164 | `razon_inliers_baja` |

El resto de los pares rechazados tiene entre 9 y 28 inliers. La tabla completa por par está en `matching_report.json` de la sesión, que no se sube al repositorio.

Los pares aceptados forman tres grupos desconectados: {1, 2}, {5, 6} y {10, 11, 12, 13}.

### Puntos por foto y tiempos (esta PC)

| Medida | Mínimo | Mediana | Máximo |
|---|---|---|---|
| Puntos SIFT por foto | 2179 | 4972 | 7471 |
| Lectura del JPEG por foto | 31 ms | 44 ms | 56 ms |
| Extracción por foto (gris + reducción + CLAHE + SIFT a 2000 px) | 325 ms | 358 ms | 393 ms |
| Emparejamiento + F/E por par | 138 ms | 253 ms | 552 ms |

| Tiempo total de la corrida | |
|---|---|
| Extracción (20 fotos) | 8,0 s |
| Pares (44) | 12,6 s |
| Total | 21,0 s |

La repetición con los mismos parámetros, que dejó el reporte final en la sesión, tardó 20,8 s y dio los mismos pares.

### Imágenes de depuración

Están en `sessions\...\debug\`. Lo que se observa en ellas:

- **`accepted_005_006.jpg`** (el aceptado más débil, razón 0,251):
  - Los inliers en verde son coherentes: líneas casi paralelas que unen las mismas partes de la piñata (cuerpo, caja de la flecha, borde del suelo).
  - La mayoría de las candidatas en rojo se cruzan en todas direcciones: son emparejamientos falsos entre escamas de papel parecidas.
- **`rejected_014_015.jpg`** (el rechazado más cercano a pasar, razón 0,224):
  - Los 56 inliers también parecen coherentes.
  - Es muy probablemente un par vecino verdadero que el filtro descarta por la razón de inliers (falso negativo), no un par malo.

## 5. Investigación de los pares rechazados

Antes de tocar parámetros se investigó la causa. Las variaciones de un parámetro a la vez se corrieron con `scanner_match` sobre la misma sesión y no se adoptaron:

| Variante | Aceptados de 44 | Razón de inliers (aceptados, mediana) | Sampson medio (mediana) | Tiempo total |
|---|---|---|---|---|
| A. Valores iniciales | **5** | 0,328 | 0,415 px | 21,0 s |
| B. `ransac_threshold_px` 2 | 9 | 0,387 | 0,795 px | 20,0 s |
| C. `ransac_threshold_px` 3 | 13 | 0,376 | 1,071 px | 20,2 s |
| D. `work_long_side` 4000 (resolución completa) | 4 | 0,312 | 0,434 px | 50,0 s |
| E. `work_long_side` 4000 y umbral 2 | 6 | 0,431 | 0,781 px | 49,8 s |
| F. `clahe` false | 4 | 0,306 | 0,417 px | 15,1 s |
| G. `ratio` 0,8 | 2 | 0,346 | 0,445 px | 23,1 s |

**Causas identificadas:**

1. **Textura repetitiva del objeto.**
   - La piñata está cubierta de escamas de papel casi idénticas (rosadas y celestes). Muchos descriptores tienen vecinos casi iguales, y el ratio test deja pasar candidatas falsas.
   - Según el par, solo entre el 8 % y el 46 % de las candidatas son inliers.
   - Subir el ratio a 0,8 empeora el resultado (variante G), lo que confirma que el problema son las candidatas ambiguas.
2. **Cambio de vista grande entre fotos vecinas.**
   - El objeto está cerca y ocupa casi toda la foto, así que un paso de ~18° cambia mucho su apariencia (hoja de contactos de la sesión: el lomo, la caja y la cabeza aparecen y desaparecen entre fotos).
   - Los pares a ~30–45° (dos pasos) casi nunca tienen más de 20 inliers.
3. **Umbral de 1 px en la resolución original.**
   - SIFT corre a 2000 px (escala 0,5), así que el umbral equivale a 0,5 px en la imagen de trabajo, que es muy estricto.
   - El error de Sampson medio de los inliers queda en ~0,4 px, casi la mitad del umbral: es la señal de que el umbral recorta la distribución del error.
   - Duplicar o triplicar el umbral (variantes B y C) acepta 9 y 13 pares.
   - Extraer a resolución completa (D y E) no compensa: más puntos y 2,5 veces más tiempo, sin más pares aceptados.
4. **Orden del anillo alterado.**
   - Las fotos 1 y 17–20 se repitieron más tarde (Sprint 1, calibración), así que sus orientaciones no siguen el anillo. Por ejemplo, 15-17 mide 6,1° y 16-17, 8,9°.
   - Por eso hay 44 pares seleccionados en vez de los 40 de un anillo regular.
5. **CLAHE ayuda.** Sin CLAHE (F) se detecta un 44 % menos de puntos (mediana 2768 frente a 4972) y se aceptan 4 pares en vez de 5.

**Lo que no es la causa:**
- F siempre se pudo estimar (0 pares con `f_no_estimada`).
- Ningún par necesitó forzar el rango 2 (`rank2_enforced = false` en los 44).
- Los inliers de los pares aceptados son geométricamente coherentes en las imágenes de depuración.

## 6. Problemas encontrados y cómo se resolvieron

| Problema | Solución |
|---|---|
| El OpenCV de vcpkg no incluía `calib3d` (`findFundamentalMat`); `features2d` con SIFT sí estaba | Se añadió la *feature* `calib3d` a `opencv4` en `server/vcpkg.json` y se recompiló OpenCV |
| `cmake` no estaba en el PATH de la consola en segundo plano | Se recarga el PATH desde el registro antes de compilar, como hace `check-env.ps1` |
| `LNK1104`: `scanner_server.exe` bloqueado por el servidor del Sprint 1, que seguía corriendo | Se detuvo el servidor antes de recompilar |
| Prueba de ida y vuelta del `.feat` en rojo: el tamaño esperado de la cabecera estaba mal calculado (36 bytes en vez de 40) | Se corrigió el cálculo (`kHeaderBytes`) |
| La prueba auxiliar de CLAHE medía la desviación global, inflada por el gradiente de luz | Se cambió a contraste local (componente de alta frecuencia). El criterio BDD (≥ 80 %) no cambió |
| La prueba de E con ruido de 0,3 px exigía una distancia menor de 0,05 a la E verdadera y daba 0,094: K amplifica el error de F | Se separó en dos pruebas: identidad exacta sin ruido (< 1e-3) y estructura esencial con ruido (< 0,2) |
| La prueba de extremo a extremo detectó que el reporte no listaba las vistas (`views` vacío) | Se corrigió `runSessionMatching` |

## 7. Versiones

Las herramientas son las mismas del Sprint 1 ([entorno.md](entorno.md)), con OpenCV 4.14 de vcpkg recompilado con `calib3d`. Sin cambios en la app móvil ni en el contrato HTTP.

## 8. Riesgos para el Sprint 3

- **Grafo de vistas fragmentado.**
  - Con los valores iniciales, la sesión de referencia deja 3 grupos desconectados de 2, 2 y 4 fotos.
  - La reconstrucción incremental del Sprint 3 necesita una cadena conectada de pares aceptados.
  - Opciones para decidir en el Sprint 3:
    - otra captura (objeto con textura menos repetitiva o con periódico debajo, pasos de ~10°, sin repetir fotos);
    - revisar `ransac_threshold_px` con datos (las variantes B y C de la sección 5);
    - ambas.
- **K aproximada.** Supone imagen sin recorte y sin distorsión. El bundle adjustment debe refinar al menos la focal.
- **Textura repetitiva.** Aun en los pares aceptados, entre el 54 % y el 75 % de las candidatas son falsas. RANSAC las filtra, pero la triangulación debe usar solo los inliers (`matches\*.csv`).
- **Orientación de la app.** Es suficiente para elegir vecinas, pero no sigue el orden del anillo cuando se repiten fotos.

## 9. Datos objetivos para la Review y la Retrospective

**Commits del sprint (rama `sprint-2/epic-2-geometria-epipolar`):**

| Commit | Contenido |
|---|---|
| `chore(server)` | calib3d en OpenCV y sección `matching` de la configuración |
| `feat(server)` HU-07 | extracción SIFT |
| `feat(server)` HU-08 | CLAHE |
| `feat(server)` HU-09 | kNN y ratio test |
| `feat(server)` HU-10 | pares vecinos |
| `feat(server)` HU-11 | F/E con RANSAC |
| `feat(server)` HU-12 | validación de pares |
| `feat(server)` | `scanner_match` y `matching_report.json` |
| `docs` | formatos y reporte |

**Datos:**
- Velocidad: 20 de 20 puntos.
- Pruebas: de 50 a 107.
- Resultado real: 5 de 44 pares aceptados, causas documentadas en la sección 5.
- Tiempo de procesamiento de una sesión de 20 fotos: unos 21 s en esta PC, solo CPU.
- La app móvil y el contrato HTTP no cambiaron.

### Opiniones del equipo

*(Para completar por el equipo en la Review y la Retrospective.)*
