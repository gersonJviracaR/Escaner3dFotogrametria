// Precisión frente a la reconstrucción de COLMAP (HU-24).
#pragma once

#include "colmap_runner.h"
#include "pipeline_params.h"
#include "sfm_types.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// to ≈ s·R·from + t
struct Similarity3 {
  double s = 1;
  cv::Matx33d R = cv::Matx33d::eye();
  cv::Vec3d t{0, 0, 0};
  cv::Vec3d apply(const cv::Vec3d& x) const { return s * (R * x) + t; }
};

// Umeyama (mínimos cuadrados). Requiere al menos 3 pares no colineales.
Similarity3 umeyama(const std::vector<cv::Vec3d>& from, const std::vector<cv::Vec3d>& to);

// RANSAC sobre muestras de 3 pares: inlier si el error tras alinear es menor que
// threshold; se reajusta con todos los inliers. Devuelve los índices inlier en `inliers`.
Similarity3 umeyamaRansac(const std::vector<cv::Vec3d>& from, const std::vector<cv::Vec3d>& to, int iterations,
                          double threshold, std::vector<int>& inliers, unsigned seed = 42);

// Ángulo (grados) de la rotación R1·R2ᵀ.
double rotationDifferenceDeg(const cv::Matx33d& R1, const cv::Matx33d& R2);

struct CameraAccuracy {
  int index = 0;
  double position_error = 0;     // en unidades de COLMAP, tras alinear
  double rotation_error_deg = 0;
  bool inlier = true;            // de la RANSAC de la alineación
};

struct AccuracyReport {
  int ours_registered = 0;
  int colmap_registered = 0;
  int common = 0;
  bool reliable = false;  // common >= min_common_cameras
  std::string note;
  double scale = 0;  // factor de la similitud (propio -> COLMAP)
  double ring_radius = 0;  // distancia media de todos los centros de COLMAP a su centroide
  double position_mean = 0, position_rms = 0, position_max = 0;
  double rotation_mean_deg = 0, rotation_max_deg = 0;
  int alignment_inliers = 0;
  std::vector<CameraAccuracy> cameras;
  double f_ours = 0, f_colmap = 0;
  double k1_ours = 0, k2_ours = 0, k1_colmap = 0, k2_colmap = 0;
  double reproj_mean_ours = 0, reproj_rms_ours = 0, reproj_mean_colmap = 0, reproj_rms_colmap = 0;
  int points_ours = 0, points_colmap = 0;
  double point_distance_mean = 0, point_distance_median = 0;  // propio alineado -> COLMAP más cercano
};

// Compara por índice de foto (los nombres se emparejan al convertir el modelo de COLMAP).
AccuracyReport compareReconstructions(const Reconstruction& ours, const Reconstruction& colmap,
                                      const AccuracyParams& params);
nlohmann::json toJson(const AccuracyReport& r);

// Referencia de COLMAP sobre las fotos indicadas: feature_extractor (una cámara RADIAL),
// exhaustive_matcher, mapper y model_converter a texto. Se queda con el modelo con más
// imágenes en out_dir/sparse_txt. Devuelve cuántas imágenes registró.
int generateColmapReference(ColmapRunner& colmap, const std::filesystem::path& image_dir,
                            const std::vector<std::string>& image_names, const std::filesystem::path& out_dir);
