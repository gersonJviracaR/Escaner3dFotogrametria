// Triangulación multivista con DLT en coordenadas homogéneas (HU-14).
#pragma once

#include "camera_model.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

// DLT: para cada vista, con P = [R | t] y la observación normalizada (a, b),
// filas a·P₃ − P₁ y b·P₃ − P₂ (normalizadas); X = vector singular de menor valor de A,
// deshomogeneizado. Requiere al menos 2 vistas.
cv::Vec4d triangulateHomogeneous(const std::vector<Pose>& poses, const std::vector<cv::Point2d>& normalized);
// Igual, deshomogeneizado (devuelve NaN si el punto está en el infinito).
cv::Vec3d triangulateDLT(const std::vector<Pose>& poses, const std::vector<cv::Point2d>& normalized);

// Ángulo (grados) entre los rayos desde los centros c1 y c2 hacia X.
double triangulationAngleDeg(const cv::Vec3d& X, const cv::Vec3d& c1, const cv::Vec3d& c2);
// El mayor ángulo entre todos los pares de centros.
double maxTriangulationAngleDeg(const cv::Vec3d& X, const std::vector<cv::Vec3d>& centers);

struct TriangulationResult {
  bool ok = false;
  std::string reason;  // "detras_de_camara", "angulo_pequeno", "error_alto"; vacío si ok
  cv::Vec3d X{0, 0, 0};
  double angle_deg = 0;     // máximo ángulo de triangulación
  double max_error_px = 0;  // máximo error de reproyección entre las vistas
};

// Triangula a partir de observaciones en píxeles (con la distorsión de `camera`) y aplica
// los controles de HU-14: delante de todas las cámaras, ángulo >= min_angle_deg y
// error de reproyección <= max_error_px en todas las vistas.
TriangulationResult triangulateChecked(const CameraModel& camera, const std::vector<Pose>& poses,
                                       const std::vector<cv::Point2d>& pixels, double min_angle_deg,
                                       double max_error_px);
