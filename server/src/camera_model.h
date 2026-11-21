// Modelo de cámara del Epic 3 (sección 3.3 del Sprint 3).
//
// Convenciones (documentadas en docs/formatos.md):
//  - Cuaterniones [w, x, y, z] de Hamilton, como el manifiesto, COLMAP y Ceres.
//  - Pose mundo -> cámara: x_cam = R·X + t; centro de la cámara C = -Rᵀ·t.
//  - Ejes de la cámara: x a la derecha, y hacia abajo, z hacia adelante.
//  - Proyección: a = x/z, b = y/z, r² = a² + b², d = 1 + k1·r² + k2·r⁴,
//    u = f·d·a + cx, v = f·d·b + cy (píxeles de la imagen original).
#pragma once

#include <opencv2/core.hpp>

#include <array>

using Quat = std::array<double, 4>;  // [w, x, y, z]

// Matriz de rotación de un cuaternión (se normaliza antes). Lanza std::invalid_argument si |q| = 0.
cv::Matx33d quaternionToRotation(const Quat& q);
// Cuaternión unitario con w >= 0 de una matriz de rotación.
Quat rotationToQuaternion(const cv::Matx33d& R);
double quaternionNorm(const Quat& q);

struct Pose {
  Quat q{1, 0, 0, 0};
  cv::Vec3d t{0, 0, 0};

  static Pose fromRt(const cv::Matx33d& R, const cv::Vec3d& t);
  cv::Matx33d R() const;
  cv::Vec3d center() const;                       // C = -Rᵀ·t
  cv::Vec3d toCamera(const cv::Vec3d& X) const;   // R·X + t
  cv::Matx34d matrix() const;                     // [R | t]
};

struct CameraModel {
  double f = 0;
  double cx = 0;
  double cy = 0;
  double k1 = 0;
  double k2 = 0;
  int width = 0;
  int height = 0;

  // Píxel de un punto en coordenadas de cámara (z debe ser > 0).
  cv::Point2d projectCamera(const cv::Vec3d& x_cam) const;
  cv::Point2d project(const Pose& pose, const cv::Vec3d& X) const;
  // Inversa de la proyección: píxel -> coordenadas normalizadas sin distorsión (a, b).
  cv::Point2d pixelToNormalized(const cv::Point2d& pixel) const;
};

// Distancia en píxeles entre la proyección de X y la observación.
double reprojectionError(const CameraModel& camera, const Pose& pose, const cv::Vec3d& X, const cv::Point2d& observed);
