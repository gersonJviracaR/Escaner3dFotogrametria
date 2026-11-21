// Parámetros del Epic 3 (reconstrucción dispersa y bundle adjustment).
// Valores iniciales configurables desde la sección "reconstruction" de la configuración.
#pragma once

#include <nlohmann/json.hpp>

#include <string>

struct ReconstructionParams {
  bool exclude_rejected_photos = true;  // excluir fotos con quality.accepted = false
  // HU-13: par inicial
  int init_min_inliers = 100;
  double init_min_median_triangulation_angle_deg = 5.0;
  // HU-14 / HU-17: triangulación y filtrado
  double tri_min_angle_deg = 1.5;
  double max_reproj_error_px = 2.0;
  int min_views = 3;
  // HU-15: registro con PnP-RANSAC
  double pnp_ransac_threshold_px = 3.0;
  double pnp_confidence = 0.999;
  int pnp_min_inliers = 20;
  // HU-16: bundle adjustment
  int ba_every = 1;  // bundle adjustment global cada ba_every cámaras registradas
  int ba_max_iterations = 100;
  double huber_delta_px = 1.0;
  bool refine_focal = true;
  bool refine_distortion = true;
};

// Aplica sobre `base` solo los campos presentes. Lanza std::invalid_argument con un
// mensaje en español si hay claves desconocidas o valores fuera de rango.
ReconstructionParams applyReconstructionJson(ReconstructionParams base, const nlohmann::json& j);
nlohmann::json toJson(const ReconstructionParams& p);

// Opciones de línea de comandos de scanner_reconstruct: "--max-reproj-error-px 3"
// equivale a {"max_reproj_error_px": 3}; "true"/"false" son booleanos, el resto números.
bool isReconstructionOverride(const std::string& option);
ReconstructionParams applyReconstructionOverride(ReconstructionParams base, const std::string& option,
                                                 const std::string& value);
