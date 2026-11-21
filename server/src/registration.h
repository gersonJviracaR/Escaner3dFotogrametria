// Registro incremental de cámaras con PnP-RANSAC y triangulación de puntos nuevos (HU-15).
#pragma once

#include "reconstruction_params.h"
#include "sfm_types.h"

#include <string>
#include <vector>

// Correspondencias 2D–3D de una foto: observaciones de pistas que ya tienen punto 3D.
struct Correspondences2D3D {
  std::vector<int> points;           // índice en rec.points
  std::vector<Observation> observations;
};
Correspondences2D3D correspondences2D3D(const Reconstruction& rec, int image);

struct RegistrationResult {
  bool ok = false;
  std::string reason;  // "pocas_correspondencias_2d3d", "pnp_fallido", "pnp_pocos_inliers"
  int n_correspondences = 0;
  int n_inliers = 0;
};

// solvePnPRansac sobre coordenadas normalizadas (umbral pnp_ransac_threshold_px / f).
// Si hay al menos pnp_min_inliers, la cámara queda registrada con su pose y sus
// observaciones inlier se agregan a los puntos.
RegistrationResult registerCamera(Reconstruction& rec, int image, const ReconstructionParams& params);

// Triangula las pistas sin punto 3D que tienen al menos 2 observaciones en cámaras
// registradas (si only_with_image >= 0, solo las que incluyen esa foto). Aplica los
// controles de HU-14 (tri_min_angle_deg, max_reproj_error_px). Devuelve cuántos agregó.
int triangulateTracks(Reconstruction& rec, const ReconstructionParams& params, int only_with_image = -1);
