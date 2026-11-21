# Entorno de desarrollo (resultado del spike SP-0)

Equipo: Windows 11 Home 10.0.26200, 64 bits. Revisión: 20 nov 2026. Para revisar otro equipo: `powershell -ExecutionPolicy Bypass -File scripts\check-env.ps1`.

## Herramientas instaladas

| Herramienta | Versión | Instalación |
|---|---|---|
| Git | 2.55.0.windows.5 | ya estaba |
| winget | 1.29.380 | ya estaba |
| CMake | 4.4.4 | `winget install --id Kitware.CMake -e` |
| Visual Studio 2022 Build Tools (MSVC) | 17.14.45, compilador 19.44.35230, Windows SDK 10.0.26100 | `winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"` |
| vcpkg | 2026-10-29 (`C:\vcpkg`, `VCPKG_ROOT` de usuario) | `git clone` + `bootstrap-vcpkg.bat -disableMetrics` |
| Flutter | 3.50.1 stable (Dart 3.14.0) en `C:\src\flutter` | `git clone -b stable`; `C:\src\flutter\bin` en el PATH de usuario |
| Android Studio | última de winget (trae el SDK y el JDK) | `winget install --id Google.AndroidStudio -e` |
| GitHub CLI | 2.105.0 | `winget install --id GitHub.cli -e` |
| Java del sistema | 26.0.2.1 | ya estaba. Gradle no la reconoce; Flutter usa el JDK de Android Studio. |
| COLMAP | 4.2.1 **con CUDA** (commit bd1fcf6), en `C:\tools\colmap` | zip `colmap-x64-windows-cuda.zip` de GitHub Releases (no se sube al repo) |

## Dependencias C++ (vcpkg, modo manifiesto, triplet `x64-windows`)

| Paquete | Versión |
|---|---|
| opencv4 [core, jpeg, png] | 4.14.0 |
| ceres | 2.2.0 (con Eigen 5.0.1, glog) |
| gtest | vcpkg 2026-10-29 |
| cpp-httplib | 0.67.0 |
| nlohmann-json | 3.12.0 |

La primera instalación de vcpkg (configurar con `cmake --preset windows-release`) tardó **13 min 24 s** en total, de los cuales 11 min fueron `vcpkg install`. Las siguientes usan la caché binaria de vcpkg.

## GPU / CUDA

```
nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv
NVIDIA GeForce RTX 3050 6GB Laptop GPU, 621.01, 6144 MiB
```

El equipo **tiene GPU NVIDIA con CUDA**, así que se usa el binario de COLMAP con CUDA. La densificación (Sprint 4) es viable con 6 GB de VRAM. Con ese límite puede hacer falta reducir `--PatchMatchStereo.max_image_size` (se evaluará en el Sprint 4). La laptop también tiene una Intel UHD Graphics integrada.

## Resultado del spike C++ (`server/spike/spike_main.cpp`)

```
OpenCV 4.14.0: spike_tablero.png (400x400), varianza del Laplaciano = 4711.06
Ceres: minimizar (x - 5)^2 desde x = 0.5 -> x = 5 (Ceres Solver Report: Iterations: 3, Initial cost: 1.012500e+01, Final cost: 1.124700e-16, Termination: CONVERGENCE)
```

OpenCV (lectura y escritura de imagen, Laplaciano), Ceres y GoogleTest compilan, enlazan y funcionan con MSVC.

## COLMAP

```
C:\tools\colmap\COLMAP.bat -h
COLMAP 4.2.1 (Commit bd1fcf6 on 2026-09-29 with CUDA, boost hash maps)
```

## Android SDK y flutter doctor

- **SDK de Android** en `%LOCALAPPDATA%\Android\Sdk`, descargado por el asistente de Android Studio al abrirlo por primera vez: platform-tools, build-tools 36.0.0 y plataforma android-37. Al compilar, Gradle agregó las plataformas 35 y 36, el NDK 28.2.13676358 y CMake 3.22.1.
- **cmdline-tools** no venía con el asistente. Se instaló a mano: `commandlinetools-win-15859902_latest.zip` de https://developer.android.com/studio (SHA-256 `90ae805d…801fb04a`, verificado), en `Sdk\cmdline-tools\latest`. También se definió `ANDROID_HOME` de usuario.
- Las licencias las aceptó el usuario con `flutter doctor --android-licenses`.
- La primera compilación de la APK de release tardó 851 s; las siguientes, unos 40 s.

```
[✓] Flutter (Channel stable, 3.50.1, on Microsoft Windows [Version 10.0.26200.9457], locale en-US)
[✓] Windows Version (Windows 11 or higher, 25H2, 2009)
[✓] Android toolchain - develop for Android devices (Android SDK version 36.0.0)
[✗] Chrome - develop for the web (Cannot find Chrome executable ...)
[✓] Visual Studio - develop Windows apps (Visual Studio Build Tools 2022 17.14.45)
[✓] Connected device (2 available)
[✓] Network resources
```

La falta de Chrome no afecta al proyecto, porque la app es solo para Android.

## Celular de pruebas

Xiaomi Poco X3 Pro (M2102J20SG, `vayu`), Android 12 (SDK 31). En MIUI hay que activar **Instalar vía USB** en las opciones de desarrollador; sin eso, `adb install` falla con `INSTALL_FAILED_USER_RESTRICTED`.

## Red y cortafuegos

El perfil de la Wi-Fi estaba en **Público** y el usuario lo cambió a **Privado**. La regla `Escaner3D Servidor` (entrada TCP 8080, perfil Privado) se creó con `scripts\open-firewall.ps1` como administrador. La conexión se comprobó desde el celular (192.168.0.235) hacia la PC (192.168.0.128:8080) con `nc` por `adb shell`: `GET /health` respondió 200.
