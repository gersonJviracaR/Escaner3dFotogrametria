# Sprint 4 (Epic 4): malla texturizada, exportación y precisión — backlog

**Meta:** a partir de la reconstrucción refinada, el sistema genera un modelo OBJ texturizado (con escala opcional), lo muestra en varias vistas y emite un reporte de precisión comparado con COLMAP.

Rama: `sprint-4/epic-4-malla-texturizada`. Estados: Pendiente / En curso / Hecha con pruebas verdes.

| ID | Historia | Pts | Prioridad | Estado |
|---|---|---|---|---|
| HU-19 | Como operador quiero densificar la reconstrucción con COLMAP usando mis poses refinadas. | 5 | Alta | Hecha con pruebas verdes |
| HU-20 | Como operador quiero generar la malla triangulada desde la nube densa. | 3 | Alta | Hecha con pruebas verdes |
| HU-21 | Como operador quiero asignar a cada triángulo la textura de su mejor cámara. | 5 | Alta | Hecha con pruebas verdes |
| HU-22 | Como usuario quiero exportar OBJ, MTL y textura (con escala opcional por distancia de referencia) listos para impresión 3D. | 2 | Alta | Hecha con pruebas verdes |
| HU-23 | Como operador quiero ver la nube y la malla en una vista general y una de detalle. | 3 | Media | Hecha con pruebas verdes |
| HU-24 | Como operador quiero un reporte de precisión comparado con la reconstrucción de COLMAP. | 2 | Media | Hecha con pruebas verdes |

**Total:** 20 puntos.

## Tareas técnicas

| Tarea | Estado |
|---|---|
| T0: sesión `tenis` (elegida por el usuario); con los valores por defecto se registran 11 de 34 cámaras, por debajo de la puerta del 70 % | Hecha |
| T0b: dos rondas de opciones nuevas (contraste SIFT, emparejamiento guiado, verificación cruzada), apagadas por defecto; ninguna mejoró las cámaras. El usuario decidió usar nuestras poses para HU-19 y las de COLMAP como comparación documentada | Hecha |
| Secciones `dense`, `mesh`, `texture`, `viewer`, `accuracy` en la configuración | Hecha |
| `scripts
| `scripts\run-pipeline.ps1` | Hecha |
| Prueba de punta a punta con `tenis` (`run-pipeline.ps1`: 78,7 s, todo ok) | Hecha |
| `docs/formatos.md`, `docs/colmap.md`, `README.md` final, `docs/sprint4-reporte.md` | Hecha |
| Comparación con poses de COLMAP (`--poses colmap`, 30 cámaras, `max_image_size` 1600); la primera corrida se detuvo por falta de memoria del sistema y se repitió | Hecha |
| OpenCV con `highgui` para la ventana de `scanner_view` (`VCPKG_MAX_CONCURRENCY=2`) | Hecha |
