# Tablero del Sprint 1: Epic 1, captura móvil y recepción en servidor local

Estados: Pendiente / En curso / Hecha (pruebas verdes) / Verificada en celular (confirmada por el usuario).

| ID | Historia | Pts | Estado | Commit |
|---|---|---|---|---|
| SP-0 | Spike de entorno | 2 | En curso (falta el SDK de Android y `flutter doctor`) | 385d7fa |
| HU-01 | Tomar fotografías desde la app | 2 | En curso | |
| HU-02 | Guardar focal, resolución y orientación por foto | 2 | En curso (pruebas Dart verdes) | |
| HU-03 | Guía de captura (≈18° por toma, contador) | 5 | En curso (pruebas Dart verdes) | |
| HU-04 | Enviar la sesión por Wi-Fi | 3 | En curso (pruebas Dart verdes) | |
| HU-05 | Servidor que almacena sesión y manifiesto | 3 | Hecha (pruebas verdes) | 8a9056c |
| HU-06 | Aviso de fotos borrosas o saturadas | 3 | Hecha (pruebas verdes) | 0a7110c |

**Total:** 20 puntos. Con pruebas verdes: 6 puntos (HU-05 y HU-06).

## Pruebas

| Suite | Pruebas | Resultado |
|---|---|---|
| C++ (`ctest`) | 47 | 47 aprobadas |
| Dart (`flutter test`) | 39 | 39 aprobadas |
| `scripts/smoke-test.ps1` | 13 verificaciones | todas correctas |
