# Tablero del Sprint 1: Epic 1, captura móvil y recepción en servidor local

Estados: Pendiente / En curso / Hecha (pruebas verdes) / Verificada en celular (confirmada por el usuario).

| ID | Historia | Pts | Estado | Commit |
|---|---|---|---|---|
| SP-0 | Spike de entorno | 2 | Hecha | 385d7fa |
| HU-01 | Tomar fotografías desde la app | 2 | Verificada en celular | 4e788e8 |
| HU-02 | Guardar focal, resolución y orientación por foto | 2 | Verificada en celular | 4e788e8 |
| HU-03 | Guía de captura (≈18° por toma, contador) | 5 | Verificada en celular | 4e788e8 |
| HU-04 | Enviar la sesión por Wi-Fi | 3 | Verificada en celular | 4e788e8 |
| HU-05 | Servidor que almacena sesión y manifiesto | 3 | Verificada en celular | 8a9056c |
| HU-06 | Aviso de fotos borrosas o saturadas | 3 | Verificada en celular | 0a7110c, d24f24a, calibración a 94 |

**Total:** 20 de 20 puntos completados.

## Pruebas

| Suite | Pruebas | Resultado |
|---|---|---|
| C++ (`ctest`) | 50 | 50 aprobadas |
| Dart (`flutter test`) | 39 | 39 aprobadas |
| `scripts/smoke-test.ps1` | 13 verificaciones | todas correctas |

El detalle de resultados y la calibración están en [sprint1-reporte.md](sprint1-reporte.md).
