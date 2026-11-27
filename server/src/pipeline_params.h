// Parámetros del Epic 4 (densificación, malla, textura, visor y precisión).
// Valores iniciales configurables desde las secciones homónimas de la configuración.
#pragma once

#include <nlohmann/json.hpp>

#include <string>

struct DenseParams {
  std::string colmap_path;    // vacío: se busca colmap en el PATH y en C:\tools\colmap
  int max_image_size = 1600;  // image_undistorter / patch_match_stereo (6 GB de GPU)
  bool geom_consistency = true;
};

struct MeshParams {
  int poisson_depth = 9;
  double trim_density = 7.0;     // PoissonMeshing.trim de COLMAP
  int min_component_faces = 100; // componentes con menos caras se eliminan
};

struct TextureParams {
  double min_cos = 0.2;          // cos(θ) mínimo entre la normal y la dirección a la cámara
  int atlas_max_size = 4096;
  int depth_map_long_side = 1000;
  int padding_px = 2;
};

struct ViewerParams {
  int width = 1280;  // las dos vistas juntas
  int height = 640;
  int max_triangles_interactive = 150000;
};

struct AccuracyParams {
  int min_common_cameras = 4;
  int ransac_iterations = 200;
};

// Cada función aplica sobre `base` solo los campos presentes y lanza
// std::invalid_argument (en español) ante claves desconocidas o valores fuera de rango.
DenseParams applyDenseJson(DenseParams base, const nlohmann::json& j);
MeshParams applyMeshJson(MeshParams base, const nlohmann::json& j);
TextureParams applyTextureJson(TextureParams base, const nlohmann::json& j);
ViewerParams applyViewerJson(ViewerParams base, const nlohmann::json& j);
AccuracyParams applyAccuracyJson(AccuracyParams base, const nlohmann::json& j);

nlohmann::json toJson(const DenseParams& p);
nlohmann::json toJson(const MeshParams& p);
nlohmann::json toJson(const TextureParams& p);
nlohmann::json toJson(const ViewerParams& p);
nlohmann::json toJson(const AccuracyParams& p);
