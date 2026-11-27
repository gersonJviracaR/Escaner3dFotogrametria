# COLMAP en el Escáner 3D (Sprint 4)

COLMAP se usa **como programa externo**, por línea de comandos, para tres cosas:
- la densificación (HU-19);
- el mallado de Poisson (HU-20);
- la reconstrucción de referencia con la que se mide la precisión (HU-24).

Nada del pipeline propio (Sprints 2 y 3) depende de COLMAP.

## Versión instalada

```
COLMAP 4.2.1 (Commit bd1fcf6 on 2026-09-29 with CUDA, boost hash maps)
```

- Instalada en `C:\tools\colmap`, con `COLMAP.bat` (que prepara el PATH de sus DLL) y `bin\colmap.exe`.
- GPU: NVIDIA GeForce RTX 3050 Laptop, 6 GB.

`scanner_dense` y `scanner_accuracy` buscan COLMAP en este orden:
1. `--colmap <ruta>` o `dense.colmap_path` de la configuración;
2. `colmap.exe` o `COLMAP.bat` en el PATH;
3. `C:\tools\colmap\COLMAP.bat` o `C:\tools\colmap\bin\colmap.exe`.

Al empezar ejecutan `colmap -h` y comprueban que la cabecera diga `with CUDA`. `patch_match_stereo` la necesita; sin CUDA, `scanner_dense` se detiene con código 3.

Toda la salida de COLMAP queda en `model\colmap.log` (o `model_colmap\colmap.log`) y en `accuracy\colmap.log`.

## Comandos usados (opciones comprobadas con `colmap <comando> -h` en 4.2.1)

### HU-19: densificación (`scanner_dense`)

```
colmap image_undistorter  --image_path <sesión> --input_path <modelo disperso> --output_path <dense>
                          --output_type COLMAP --max_image_size 1600
colmap patch_match_stereo --workspace_path <dense> --workspace_format COLMAP
                          --PatchMatchStereo.geom_consistency 1 --PatchMatchStereo.max_image_size 1600
colmap stereo_fusion      --workspace_path <dense> --workspace_format COLMAP
                          --input_type geometric --output_path <dense>\fused.ply
```

- **Modelo disperso de entrada:** `colmap_ours\sparse\0` con nuestras poses (por defecto), o `colmap_ref\sparse_txt` (`--poses colmap`, comparación).
- **Formato:** COLMAP lee el modelo de texto directamente.
- **Imágenes:** `image_undistorter` solo procesa las imágenes del modelo (las cámaras registradas). No hace falta copiar fotos: se le pasa la carpeta de la sesión.
- **Sin consistencia geométrica** (`dense.geom_consistency: false`): se pasa `geom_consistency 0` y la fusión usa `--input_type photometric`.

### HU-20: malla (`scanner_dense`)

```
colmap poisson_mesher --input_path <dense>\fused.ply --output_path <model>\mesh_raw.ply
                      --PoissonMeshing.depth 9 --PoissonMeshing.trim 7
```

- **Valores por defecto de la herramienta:** `depth` 13 y `trim` 10. Usamos los de la configuración (`mesh.poisson_depth`, `mesh.trim_density`).
- **Limpieza posterior:** la hace nuestro código (`meshing.cpp`).
- **Densidad por vértice:** el PLY de salida no la trae, así que el recorte por baja densidad es el `trim` del propio Poisson.

### HU-24: referencia (`scanner_accuracy`)

```
colmap feature_extractor  --database_path <ref>\database.db --image_path <sesión>
                          --image_list_path <ref>\image_list.txt
                          --ImageReader.single_camera 1 --ImageReader.camera_model RADIAL
colmap exhaustive_matcher --database_path <ref>\database.db
colmap mapper             --database_path <ref>\database.db --image_path <sesión> --output_path <ref>\sparse
colmap model_converter    --input_path <ref>\sparse\<k> --output_path <ref>\txt_<k> --output_type TXT
```

- `image_list.txt` contiene solo las fotos aceptadas por calidad (las mismas candidatas que nuestro pipeline).
- `single_camera 1` y el modelo `RADIAL` (f, cx, cy, k1, k2) son nuestro modelo de cámara, para una comparación justa.
- Si el mapper deja varios modelos, se usa el de más imágenes, que queda en `<ref>\sparse_txt`.

## Diferencias y trampas encontradas

| Tema | Detalle |
|---|---|
| Nombres de opciones | En 4.2.1 las de extracción son `FeatureExtraction.*` / `SiftExtraction.*`, y las del emparejamiento `FeatureMatching.*`; en versiones 3.x eran `SiftExtraction.use_gpu` / `SiftMatching.use_gpu`. Este proyecto no las necesita, porque los valores por defecto usan la GPU. |
| `model_converter` | En 4.2.1 **termina sin mensaje** (código −1073740791, 0xC0000409) si la carpeta de salida `TXT` no existe. Se crea antes de llamarlo. |
| Centro de píxel | COLMAP pone el centro del primer píxel en (0,5; 0,5) y nosotros en (0; 0). Al exportar se suma 0,5 al punto principal y a las observaciones; al leer se resta. |
| Pose | Igual en ambos: mundo → cámara `x = R·X + t`, cuaternión `[w, x, y, z]`. Se exporta sin transformar. |
| Memoria de GPU | Con `max_image_size` 1600, `patch_match_stereo` usó ~1,4 GB de los 6 GB en `tenis`. No hizo falta bajarlo. |
