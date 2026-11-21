# Sprint 2 (Epic 2): características y geometría epipolar — backlog

**Meta:** para cada par de vistas vecinas de una sesión existente, el servidor entrega correspondencias SIFT validadas con la geometría epipolar (matrices F y E) y descarta los pares inválidos.

Rama: `sprint-2/epic-2-geometria-epipolar`. Estados: Pendiente / En curso / Hecha con pruebas verdes.

| ID | Historia | Pts | Prioridad | Estado |
|---|---|---|---|---|
| HU-07 | Como operador quiero extraer características SIFT de cada foto. | 3 | Alta | Hecha con pruebas verdes |
| HU-08 | Como operador quiero normalizar el contraste antes de extraer características para tolerar iluminación variable. | 2 | Media | Hecha con pruebas verdes |
| HU-09 | Como operador quiero emparejar descriptores entre pares de fotos con ratio test. | 5 | Alta | Hecha con pruebas verdes |
| HU-10 | Como operador quiero emparejar solo vistas vecinas (según la orientación registrada) para reducir el cómputo. | 2 | Media | Pendiente |
| HU-11 | Como operador quiero estimar F con RANSAC y obtener E con K para validar la geometría de cada par. | 5 | Alta | Pendiente |
| HU-12 | Como operador quiero rechazar pares con desviación epipolar mayor al umbral para descartar falsos emparejamientos. | 3 | Alta | Pendiente |

**Total:** 20 puntos.

## Tareas técnicas

| Tarea | Estado |
|---|---|
| OpenCV de vcpkg con `calib3d` (`findFundamentalMat`); `features2d` ya incluía SIFT | Hecha |
| Sección `matching` en `config.default.json` y en la lectura de configuración (con pruebas) | Hecha |
| Escritura segura reutilizable (`atomic_file`) | Hecha |
| Herramienta `scanner_match` y `matching_report.json` | Pendiente |
| Prueba con la sesión de referencia `20261120-212147-llama` | Pendiente |
| `docs/formatos.md`, `README.md`, `docs/sprint2-reporte.md` | Pendiente |
