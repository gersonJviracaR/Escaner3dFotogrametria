# Escáner 3D por Fotogrametría con Smartphone

Proyecto del segundo parcial de *Graficación por Computadora*, Ingeniería Informática, UMSS (Cochabamba, Bolivia).
Docente: Ing. Abraham Mamani Veliz. Programadores: Gerson Jared Viraca Rodriguez y Joel Matías Mantilla Villaca.

El usuario toma con un celular Android unas 20 fotos alrededor de un objeto, guiado por la app. Las fotos viajan por Wi-Fi a la PC, donde un servidor en **C++17** las valida y, en los siguientes sprints, reconstruye la geometría 3D (SfM propio con OpenCV y Ceres; densificación y malla con COLMAP) hasta exportar un OBJ texturizado. **El celular no procesa imágenes: solo captura y envía.**

```
Celular Android (Flutter)  ──HTTP/Wi-Fi local──▶  Servidor local en la PC (C++17)
 captura guiada + IMU                              recepción → control de calidad (OpenCV)
 envío de la sesión                                → [Sprints 2-4] SIFT → epipolar → triangulación
                                                     → Ceres BA → COLMAP densa/malla → OBJ + reporte
```

## Estado

| Sprint | Epic | Estado |
|---|---|---|
| 1 | Captura móvil y recepción en servidor local | **Completo** (ver [docs/sprint1-reporte.md](docs/sprint1-reporte.md)) |
| 2 | Características y geometría epipolar | **Completo** (ver [docs/sprint2-reporte.md](docs/sprint2-reporte.md)) |
| 3 | Reconstrucción dispersa y bundle adjustment | **Completo**, con resultado parcial en la sesión real: 11 de 34 cámaras (ver [docs/sprint3-reporte.md](docs/sprint3-reporte.md)) |
| 4 | Malla texturizada y exportación | Pendiente |

## Estructura

```
server/    servidor C++17 (CMake + vcpkg): HTTP, sesiones, manifiesto, control de calidad;
           tools/scanner_match.cpp (SIFT, pares vecinos, F y E por sesión);
           tools/scanner_reconstruct.cpp (triangulación, PnP, bundle adjustment con Ceres)
mobile/    app Flutter para Android (captura guiada y envío)
scripts/   check-env.ps1, open-firewall.ps1, smoke-test.ps1 (PowerShell)
docs/      entorno.md, api.md, formatos.md, sprint1-backlog.md, sprint1-reporte.md,
           sprint2-backlog.md, sprint2-reporte.md, sprint3-backlog.md, sprint3-reporte.md
```

## Requisitos (Windows 11)

- Git, CMake ≥ 3.21 y Visual Studio 2022 Build Tools con la carga de trabajo de C++.
- vcpkg en `C:\vcpkg`, con la variable de usuario `VCPKG_ROOT=C:\vcpkg`.
- Flutter (canal stable) y Android Studio (aporta el SDK de Android y el JDK), solo si vas a compilar la app.
- Opcional, para los Sprints 2 a 4: COLMAP en `C:\tools\colmap` y una GPU NVIDIA con CUDA.

Para ver qué tienes instalado y qué falta:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\check-env.ps1
```

[docs/entorno.md](docs/entorno.md) tiene los comandos de instalación exactos y las versiones usadas.

## Servidor

### Compilar y probar

```powershell
cd server
cmake --preset windows-release            # la primera vez compila las dependencias con vcpkg (10-90 min)
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Ejecutar

```powershell
.\build\Release\scanner_server.exe --port 8080 --data-dir ..\sessions
```

Al arrancar, el servidor muestra las direcciones que hay que escribir en la app, por ejemplo `192.168.0.128:8080`. Cada sesión se guarda en `sessions\<id>\` con sus `img_###.jpg` y un `manifest.json`.

| Opción | Descripción |
|---|---|
| `--port N` | Puerto (8080 por defecto). |
| `--data-dir ruta` | Carpeta de las sesiones. |
| `--config archivo.json` | Sobrescribe los valores de `config.default.json` (umbrales de calidad, puerto, tamaño máximo). |

El contrato HTTP y el formato del manifiesto están en [docs/api.md](docs/api.md).

### Abrir el cortafuegos (una vez, como administrador)

```powershell
powershell -ExecutionPolicy Bypass -File scripts\open-firewall.ps1
```

Crea una regla de entrada para el puerto TCP 8080 en el perfil **Privado**. La red Wi-Fi debe estar marcada como **Privada** (Configuración → Red e Internet → Wi-Fi → propiedades de la red).

### Prueba de humo

Con el servidor corriendo:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\smoke-test.ps1
```

### Emparejar una sesión (Sprint 2)

`scanner_match` procesa una sesión **ya almacenada**. Para cada foto aceptada extrae puntos SIFT (con CLAHE) y elige los pares vecinos según la orientación registrada. Para cada par, empareja con ratio test, estima F con RANSAC y E = K'ᵀ F K, y descarta los pares inválidos.

```powershell
cd server
.\build\Release\scanner_match.exe --session ..\sessions\20261120-212147-llama --debug
```

| Opción | Descripción |
|---|---|
| `--session carpeta` | Sesión a procesar (obligatoria). |
| `--config archivo.json` | Sobrescribe la sección `matching` de `config.default.json`. |
| `--debug` | Guarda en `debug\` una imagen con las correspondencias de un par aceptado y de uno rechazado. |
| `--<parámetro> valor` | Sobrescribe un parámetro, p. ej. `--max-angle-deg 30`, `--ransac-threshold-px 2`, `--clahe false`. |

Imprime una tabla por par y un resumen con los tiempos. En la sesión deja:
- `features\img_###.feat`;
- `matches\pair_###_###.csv` (solo para los pares aceptados);
- `matching_report.json`.

Los formatos están en [docs/formatos.md](docs/formatos.md).

Desde el Sprint 3 hay dos opciones que amplían la selección de pares: `--neighbor-index-window N` y `--ring-closure true`. Por defecto están en `0` / `false`, que es la selección del Sprint 2. Más detalles en [docs/formatos.md](docs/formatos.md).

### Reconstruir una sesión (Sprint 3)

`scanner_reconstruct` toma una sesión **ya procesada por `scanner_match`** y hace la reconstrucción incremental:
1. elige el par inicial y recupera su pose (E y `recoverPose`);
2. triangula con DLT;
3. registra las demás cámaras con PnP-RANSAC;
4. refina con bundle adjustment en Ceres (poses, puntos, `f`, `k1` y `k2`);
5. filtra los puntos vistos en menos de 3 cámaras o con error alto.

Al final reporta el error de reproyección por cámara.

```powershell
cd server
.\build\Release\scanner_match.exe --session ..\sessions\20261121-142259-tenis
.\build\Release\scanner_reconstruct.exe --session ..\sessions\20261121-142259-tenis --debug
```

| Opción | Descripción |
|---|---|
| `--session carpeta` | Sesión a procesar (obligatoria; debe tener `matching_report.json`). |
| `--config archivo.json` | Sobrescribe la sección `reconstruction` de `config.default.json`. |
| `--include-rejected` | Incluye como candidatas las fotos rechazadas por calidad. Por defecto se excluyen. |
| `--debug` | Guarda una vista superior y otra lateral de la nube con las cámaras. |
| `--<parámetro> valor` | Sobrescribe un parámetro, p. ej. `--max-reproj-error-px 3`, `--pnp-min-inliers 10`, `--refine-distortion false`. |

Imprime una tabla por cámara (error medio y RMS antes y después del BA final) y un resumen con los tiempos. Deja en `sessions\<id>\reconstruction\`:
- `reconstruction.json`;
- `reconstruction_report.json`;
- `sparse_cloud.ply`, que se abre con MeshLab o CloudCompare; los vértices magenta son las cámaras.

## App Android

### Compilar

```powershell
cd mobile
flutter pub get
flutter test
flutter build apk --release
New-Item -ItemType Directory -Force ..\dist | Out-Null
Copy-Item build\app\outputs\flutter-apk\app-release.apk ..\dist\escaner3d-0.1.apk
```

Requiere Android 8.0 (API 26) o superior. La APK de release se firma con la clave de depuración por defecto, lo que basta para instalarla directamente (sideload) en esta fase. La APK no se sube al repositorio.

### Instalar

- **Por USB:** activa en el celular *Opciones de desarrollador → Depuración USB*, conéctalo, acepta la huella RSA y ejecuta `adb install -r dist\escaner3d-0.1.apk`.
- **Sin USB:** copia la APK al celular y ábrela. Android pedirá permitir la instalación de apps de orígenes desconocidos para la app con la que la abras (Archivos, Drive, etc.).

### Usar

1. Enciende el servidor en la PC. El celular y la PC deben estar en la misma Wi-Fi.
2. En la app, escribe la dirección `IP:puerto` que muestra el servidor y pulsa **Probar conexión**.
3. Escribe un nombre para el objeto y pulsa **Nueva sesión**.
4. Gira alrededor del objeto. La guía indica cuándo tomar cada foto, aproximadamente cada 18°: verde = listo, naranja = sigue girando, rojo = giraste demasiado. El contador muestra `n / 20`.
5. Pulsa **Enviar sesión**, que se habilita desde 12 fotos. La app sube las fotos una a una y luego muestra las rechazadas por estar **borrosas** o **saturadas**.
6. Pulsa **Repetir foto(s) rechazada(s)**, vuelve a tomarlas desde la misma posición y la app las reenvía.

## Tecnologías

C++17, CMake, vcpkg, OpenCV, Ceres Solver, cpp-httplib, nlohmann-json, GoogleTest y COLMAP en el servidor; Flutter (Dart) y Kotlin, para el sensor de orientación, en la app. Por restricción de la materia, el proyecto no usa Python.

## Licencia

Por definir.
