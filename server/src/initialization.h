// Par inicial y su pose relativa (HU-13).
#pragma once

#include "camera_model.h"
#include "reconstruction_params.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

// Par aceptado del Sprint 2 con sus correspondencias inlier (píxeles de la imagen original).
struct PairCandidate {
  int i = 0;
  int j = 0;
  cv::Matx33d E = cv::Matx33d::zeros();  // del matching_report.json (x_jᵀ E x_i = 0, normalizadas)
  std::vector<cv::Point2d> pts_i;
  std::vector<cv::Point2d> pts_j;
  int n_inliers() const { return static_cast<int>(pts_i.size()); }
};

// Pose de la cámara j respecto a la i (la i queda en el origen): x_j = R·x_i + t, |t| = 1.
struct RelativePose {
  bool ok = false;
  Pose pose_j;
  int n_points = 0;
  int n_front = 0;          // triangulados delante de ambas cámaras
  double front_ratio = 0;
  double median_angle_deg = 0;  // ángulo de triangulación mediano de los puntos delante
};

// cv::recoverPose con E sobre las coordenadas normalizadas sin distorsión, y luego
// triangulación DLT de todas las correspondencias para medir cuántas quedan delante.
RelativePose recoverRelativePose(const CameraModel& camera, const PairCandidate& pair);

struct InitialPairChoice {
  int i = -1;
  int j = -1;
  std::string reason;  // "elegido", "pocos_inliers", "pocos_puntos_delante", "angulo_mediano_bajo"
  int n_inliers = 0;
  double front_ratio = 0;
  double median_angle_deg = 0;
};

struct InitialPair {
  bool found = false;
  int i = -1;
  int j = -1;
  RelativePose relative;
  std::vector<InitialPairChoice> evaluated;  // en el orden en que se evaluaron
};

// Recorre los pares de más a menos inliers y elige el primero con al menos
// init_min_inliers, más de la mitad de los puntos delante de ambas cámaras y
// ángulo de triangulación mediano >= init_min_median_triangulation_angle_deg.
InitialPair chooseInitialPair(const CameraModel& camera, const std::vector<PairCandidate>& pairs,
                              const ReconstructionParams& params);
