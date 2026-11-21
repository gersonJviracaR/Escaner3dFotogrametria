// Geometría epipolar de un par de vistas: F con RANSAC, E = K'ᵀ F K (HU-11).
//
// Convención: x  = punto en la foto i, x' = punto en la foto j, ambos en píxeles de la
// imagen original y en coordenadas homogéneas (u, v, 1); se cumple x'ᵀ F x = 0.
#pragma once

#include "matching_params.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

// Iteraciones máximas de RANSAC (el valor por defecto de OpenCV, 1000, no basta
// con razones de inliers bajas).
constexpr int kRansacMaxIters = 10000;

// Distancia de Sampson (implementación propia), en píxeles:
//   d² = (x'ᵀ F x)² / ((F x)₁² + (F x)₂² + (Fᵀ x')₁² + (Fᵀ x')₂²);  devuelve √d².
double sampsonDistance(const cv::Matx33d& F, const cv::Point2d& x, const cv::Point2d& x_prime);

// Proyecta F a rango 2 anulando su menor valor singular.
cv::Matx33d enforceRank2(const cv::Matx33d& F);

// E = K'ᵀ F K proyectada a la variedad esencial: valores singulares (s, s, 0) con
// s = (σ₁ + σ₂) / 2. K es la de la foto i y K' la de la foto j.
cv::Matx33d essentialFromFundamental(const cv::Matx33d& F, const cv::Matx33d& K, const cv::Matx33d& K_prime);

struct PairGeometry {
  bool f_estimated = false;
  bool rank2_enforced = false;  // true si la F de OpenCV no tenía rango 2 numérico
  cv::Matx33d F = cv::Matx33d::zeros();
  cv::Matx33d E = cv::Matx33d::zeros();
  int n_matches = 0;
  int n_inliers = 0;
  double inlier_ratio = 0;
  double mean_sampson_px = 0;  // media de la distancia de Sampson sobre los inliers
  std::vector<int> inliers;    // posiciones en los vectores de entrada
  // HU-12: validación del par. Un par rechazado no pasa al Sprint 3.
  bool accepted = false;
  std::vector<std::string> reasons;
};

// Motivos de rechazo (HU-12), en este orden: "f_no_estimada", "pocos_inliers"
// (n_inliers < min_inliers), "razon_inliers_baja" (inlier_ratio < min_inlier_ratio).
// Vacío si el par es válido.
std::vector<std::string> rejectionReasons(bool f_estimated, int n_inliers, double inlier_ratio,
                                          const MatchingParams& params);

// F con cv::findFundamentalMat (FM_RANSAC, ransac_threshold_px, ransac_confidence),
// luego inliers recalculados con sampsonDistance <= ransac_threshold_px, y validación.
PairGeometry estimatePairGeometry(const std::vector<cv::Point2f>& pts_i, const std::vector<cv::Point2f>& pts_j,
                                  const cv::Matx33d& K_i, const cv::Matx33d& K_j, const MatchingParams& params);
