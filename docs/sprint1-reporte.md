# Reporte del Sprint 1: Epic 1, captura móvil y recepción en servidor local

Datos medidos el 20 nov 2026 en la PC del equipo (Windows 11) y con un Xiaomi Poco X3 Pro (Android 12). Las opiniones del equipo para la Sprint Review y la Retrospective las completa el equipo en la última sección.

## 1. Historias completadas

| ID | Historia | Pts | Estado |
|---|---|---|---|
| SP-0 | Spike de entorno | 2 | Hecha |
| HU-01 | Tomar fotografías desde la app | 2 | Verificada en celular |
| HU-02 | Guardar focal, resolución y orientación por foto | 2 | Verificada en celular |
| HU-03 | Guía de captura (≈18° por toma, contador) | 5 | Verificada en celular |
| HU-04 | Enviar la sesión por Wi-Fi | 3 | Verificada en celular |
| HU-05 | Servidor que almacena sesión y manifiesto | 3 | Verificada en celular |
| HU-06 | Aviso de fotos borrosas o saturadas | 3 | Verificada en celular |
| | **Total** | **20 / 20** | |

Lo que se verificó en el celular:
- **HU-01:** se tomaron 40 fotos reales en dos sesiones.
- **HU-02:** las 40 fotos tienen en el manifiesto la resolución, la focal del EXIF (4.71 mm, 25 mm equivalentes) y el cuaternio de orientación.
- **HU-03:** el usuario confirmó que los colores verde, naranja y rojo de la guía funcionan correctamente.
- **HU-04:** el envío, la lista de rechazadas y la repetición de tomas funcionaron en las sesiones `20261120-204353-objeto` y `20261120-212147-llama`.

## 2. Pruebas automatizadas

| Suite | Cantidad | Resultado |
|---|---|---|
| C++ (GoogleTest + `ctest`) | 50 | 50 aprobadas |
| Dart (`flutter test`) | 39 | 39 aprobadas |
| `scripts/smoke-test.ps1` (curl.exe contra el servidor real) | 13 verificaciones | todas correctas |
| `flutter analyze` | — | sin observaciones |

Desglose de las pruebas C++:

| Archivo | Pruebas |
|---|---|
| `test_quality.cpp` | 11 |
| `test_manifest.cpp` | 10 |
| `test_session_store.cpp` | 13 |
| `test_http_integration.cpp` | 11 |
| `test_config.cpp` | 5 |

Desglose de las pruebas Dart:

| Archivo | Pruebas |
|---|---|
| `capture_guide_test.dart` | 14 |
| `photo_metadata_test.dart` | 10 |
| `scanner_api_test.dart` | 10 |
| `capture_session_test.dart` | 3 |
| `widget_test.dart` | 2 |

## 3. Calibración del control de calidad

### Métrica final

- **Nitidez:** la imagen se pasa a gris y se redimensiona para que su lado mayor mida 1000 px. Luego se calcula la varianza del Laplaciano **solo en el 50 % central** (la mitad del ancho y la mitad del alto).
- **Umbral: `sharpness_min = 94`**, en `server/config.default.json` y como valor por defecto en el código.
- **Saturación:** fracción de píxeles con gris ≥ 250 en toda la imagen, con `overexposed_max = 0.15`. Este umbral **no se modificó** (ver límites).

### Cómo se obtuvo

1. **Umbral inicial de 100 sobre la imagen completa.** Con la sesión `20261120-204353-objeto` (1920×1080) rechazó 4 fotos nítidas: la 6, la 17, la 18 y la 19, con 69.3, 59.1, 93.4 y 89.7. El fondo liso y desenfocado bajaba la varianza de toda la imagen.
2. **Comparación de regiones** en las 20 fotos buenas de esa sesión, con una herramienta C++ de análisis fuera del repo:

   | Región | Mínimo de las 20 buenas |
   |---|---|
   | Imagen completa | 59.1 |
   | Centro 60 % | 98.0 |
   | **Centro 50 %** | **105.8** |
   | Centro 33 % | 142.4 |

   Se eligió el 50 % central (aprobado por el equipo). El 33 % depende demasiado del encuadre. Se fijó **90 como umbral provisional**.
3. **Sesión de calibración** `20261120-212147-llama`: 4000×3000, un mismo objeto (una piñata), luz interior baja. El usuario etiquetó las fotos 2–16 como buenas y las 17–20 como movidas a propósito.

   | Grupo | n | Nitidez (centro 50 %) |
   |---|---|---|
   | Buenas (2–16) | 15 | mín. **104.6** (foto 3); resto: 124.2, 141.9, 182.8, 183.7, 261.5, 312.2, 315.2, 320.6, 339.5, 348.7, 372.2, 382.6, 415.2, 418.1 |
   | Movidas (17–20) | 4 | **84.2** (17), 17.1, 11.8, 11.6 |
   | Foto 1 (dudosa) | 1 | 66.8. Excluida: está descentrada (el objeto a la izquierda, cama y fondo al centro) y además se ve algo más oscura y suave que la 2. Con la métrica sola no se puede separar ambas causas. |

   El umbral se eligió como el **punto medio geométrico** entre la peor foto buena y la mejor movida: √(104.6 × 84.2) ≈ 93.8, redondeado a **94**. Así queda un margen del 10 % bajo la peor foto buena y del 12 % sobre la mejor movida.

   | Umbral | Buenas aceptadas (15 de llama + 20 de objeto) | Movidas rechazadas (4) |
   |---|---|---|
   | 90 | 35/35 | 4/4 |
   | **94** | **35/35** | **4/4** |
   | 100 | 35/35 | 4/4 |

### Límites de la calibración

- **Solo 4 fotos movidas**, y una sola (la 17, con 84.2) está cerca del umbral. El intervalo entre clases (84.2 a 104.6) es estrecho.
- **Un solo objeto** (la piñata), en un solo ambiente (dormitorio, luz interior baja) y con **un solo celular** (Poco X3 Pro).
- **La saturación no se probó con fotos reales.** En las 40 fotos del celular el máximo fue 0.0055 (0.55 %), lejos del umbral de 0.15. El umbral de saturación sigue siendo el valor inicial y solo está validado con imágenes sintéticas.
- **Los originales de la calibración ya no están en disco.** Después de la medición, el usuario repitió las fotos 1 y 17–20 desde la app (21:33) y se aceptaron con nitidez 279.3, 191.1, 406.3, 160.2 y 677.8. Los valores de calibración vienen de la medición previa, que coincide con el log del servidor de las 21:24.
- **La sesión `20261120-203853-objeto`** (1920×1080, las 20 rechazadas con 20 a 53 en la imagen completa, máximo 81.7 en el 50 % central) no se usó porque no está etiquetada.

Conviene repetir la calibración con más objetos, con luz de día y con fotos sobreexpuestas reales.

## 4. Sesiones de ejemplo

| Sesión | Resolución | Archivos | Tamaño total | Resultado final |
|---|---|---|---|---|
| `20261120-204353-objeto` | 1920×1080 (`ResolutionPreset.veryHigh`) | 20 JPG + `manifest.json` | 8.5 MB (0.45 MB/foto) | 16/20 con el umbral de 100 sobre la imagen completa (con la métrica final serían 20/20) |
| `20261120-212147-llama` | **4000×3000** (`ResolutionPreset.max`) | 20 JPG + `manifest.json` | 38.1 MB tras repetir 5 tomas (36.5 MB en el primer envío; 1.47–2.08 MB/foto) | 15/20 en el primer envío y 20/20 tras repetir 1 y 17–20 |

**Tiempo de envío de 20 fotos a 12 MP:** unos **11.6 s**, es decir 0.58 s por foto y unos 3.1 MB/s, por la Wi-Fi local. Se estimó con la hora de escritura de cada archivo en el servidor: 11.0 s entre la primera y la última foto, más la primera subida. La repetición de 5 fotos tardó unos 3 s.

**Resolución:** al principio la app usaba `ResolutionPreset.veryHigh`, que en el Poco X3 Pro entrega solo 1920×1080. Se cambió a `ResolutionPreset.max`, que da 4000×3000 (12 MP), para tener más detalle en la reconstrucción de los Sprints 2 a 4.

## 5. Problemas encontrados y soluciones

| Problema | Solución |
|---|---|
| El cortafuegos de Windows bloquearía el puerto 8080 y la Wi-Fi estaba en perfil **Público**. | El usuario cambió la red a **Privada** y ejecutó `scripts/open-firewall.ps1` como administrador. Se verificó desde el celular con `nc` por `adb shell`. |
| HTTP sin cifrar bloqueado por Android. | `android:usesCleartextTraffic="true"` en el `AndroidManifest.xml`. |
| El asistente de Android Studio no instaló **cmdline-tools**, que `flutter doctor --android-licenses` necesita. | Se descargó de Google, se verificó el SHA-256 y se instaló en `Sdk\cmdline-tools\latest`, con `ANDROID_HOME`. |
| `adb install` en MIUI falló con `INSTALL_FAILED_USER_RESTRICTED`. | Activar **Instalar vía USB** en las opciones de desarrollador del celular. |
| Java 26 del sistema es más nueva que la que reconoce Gradle. | Flutter usa el JDK de Android Studio; no hizo falta cambiar nada. |
| Flutter 3.50 marca como obsoleto `package:flutter/material.dart`. | Se usa el paquete `material_ui`. |
| `.gitignore` con `data/` ignoraba `server/tests/data`. | Se ancló a la raíz: `/data/`. |
| La nitidez sobre la imagen completa rechazaba fotos nítidas con fondo liso. | Métrica en el 50 % central y umbral recalibrado a 94 (sección 3). |
| `veryHigh` entregaba solo 1080p. | `ResolutionPreset.max` (4000×3000). |
| Al repetir tomas se sobrescriben las originales. | Es el comportamiento especificado (reemplazo por `index`). Hay que tenerlo en cuenta en futuras sesiones de calibración, guardando una copia antes de repetir. |
| EXIF | Sin problemas en el Poco X3 Pro: `FocalLength` = 4.71 mm y `FocalLengthIn35mmFilm` = 25 en todas las fotos. La ruta de `null` + advertencia solo se probó con pruebas unitarias. |

## 6. Versiones de herramientas

Detalle en [entorno.md](entorno.md): Windows 11 Home 10.0.26200, MSVC 19.44 (VS Build Tools 17.14.45), CMake 4.4.4, vcpkg 2026-10-29, OpenCV 4.14.0, Ceres 2.2.0, Eigen 5.0.1, cpp-httplib 0.67.0, nlohmann-json 3.12.0, Flutter 3.50.1 (Dart 3.14.0), Android SDK 36 / NDK 28.2, camera 0.12.2, http 1.6.0, exif 3.3.0 y COLMAP 4.2.1 con CUDA.

## 7. Hallazgo de GPU / CUDA

La PC tiene una **NVIDIA GeForce RTX 3050 Laptop de 6 GB** (driver 621.01), y COLMAP 4.2.1 con CUDA funciona. La densificación del Sprint 4 es viable. Con 6 GB de VRAM y fotos de 12 MP probablemente haya que limitar `PatchMatchStereo.max_image_size`; se evaluará en el Sprint 4.

## 8. Datos para la Sprint Review y la Retrospective

**Demostrado en el sprint:**
- Captura guiada de 20 fotos a 12 MP con un celular Android real.
- Envío por Wi-Fi de 20 fotos en unos 12 s.
- Detección de 4 de 4 fotos movidas y aceptación de 35 de 35 fotos buenas con el umbral calibrado.
- Repetición de tomas rechazadas desde la app.
- Sesión guardada con el manifiesto completo: focal, resolución real y cuaternio en cada foto.

**Fuera del sprint, por alcance:** SIFT, emparejamiento, geometría epipolar, triangulación, bundle adjustment, COLMAP denso y mallado (Sprints 2 a 4).

**Pendientes y riesgos para el Sprint 2:**
- La calibración se apoya en pocos datos (ver límites en la sección 3).
- El umbral de saturación no está probado con fotos reales.
- Las sesiones se guardan en memoria hasta `complete`. Si el servidor se reinicia, una sesión a medio enviar no se recupera, aunque las fotos sí quedan en disco.
- La APK está firmada con la clave de depuración.

**Opiniones del equipo:** *(las completa el equipo)*
