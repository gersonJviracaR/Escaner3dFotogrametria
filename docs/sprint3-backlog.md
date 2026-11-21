# Sprint 3 (Epic 3): reconstrucción dispersa y bundle adjustment — backlog

**Meta:** a partir de los pares validados de una sesión, el servidor reconstruye una nube de puntos dispersa con las poses de cámara optimizadas y reporta el error de reproyección por cámara.

Rama: `sprint-3/epic-3-reconstruccion-dispersa`. Estados: Pendiente / En curso / Hecha con pruebas verdes.

| ID | Historia | Pts | Prioridad | Estado |
|---|---|---|---|---|
| HU-13 | Como operador quiero elegir un par inicial y recuperar su pose relativa (R,t). | 3 | Alta | Hecha con pruebas verdes |
| HU-14 | Como operador quiero triangular puntos 3D con coordenadas homogéneas (DLT). | 3 | Alta | Hecha con pruebas verdes |
| HU-15 | Como operador quiero registrar las demás cámaras con PnP-RANSAC y triangular nuevos puntos. | 5 | Alta | Pendiente |
| HU-16 | Como operador quiero refinar poses y puntos con bundle adjustment en Ceres. | 5 | Alta | Pendiente |
| HU-17 | Como operador quiero descartar puntos visibles en menos de 3 cámaras o con error de reproyección alto. | 3 | Alta | Pendiente |
| HU-18 | Como operador quiero ver el error de reproyección de cada cámara. | 1 | Media | Pendiente |

**Total:** 20 puntos.

## Tareas técnicas

| Tarea | Estado |
|---|---|
| Paso 0: grafo de vistas de la sesión `tenis` con `scanner_match` por defecto. **No conexo** (30 + 4 vistas); el usuario decidió seguir con el componente de 30 vistas | Hecha |
| Sección `reconstruction` en `config.default.json` y en la lectura de configuración (con pruebas) | Hecha |
| Modelo de cámara (HU-14), pistas (*tracks*) | Hecha |
| Herramienta `scanner_reconstruct`, `reconstruction.json`, `reconstruction_report.json`, `sparse_cloud.ply` | Pendiente |
| Prueba con la sesión `20261121-142259-tenis` | Pendiente |
| `docs/formatos.md`, `README.md`, `docs/sprint3-reporte.md` | Pendiente |
