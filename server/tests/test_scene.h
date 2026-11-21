// Escenas sintéticas para las pruebas del Epic 3: puntos 3D conocidos y cámaras con
// K, R y t conocidos alrededor del origen.
#pragma once

#include "camera_model.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace scene {

constexpr double kPi = 3.14159265358979323846;

inline CameraModel camera(double f = 900.0) {
  CameraModel c;
  c.f = f;
  c.cx = 500;
  c.cy = 375;
  c.width = 1000;
  c.height = 750;
  return c;
}

// Cámara en C mirando hacia `target`; el mundo tiene z hacia arriba.
inline Pose lookAt(const cv::Vec3d& C, const cv::Vec3d& target = {0, 0, 0}) {
  const cv::Vec3d up(0, 0, 1);
  const cv::Vec3d z = cv::normalize(target - C);
  const cv::Vec3d x = cv::normalize(z.cross(up));
  const cv::Vec3d y = z.cross(x);
  const cv::Matx33d R(x[0], x[1], x[2], y[0], y[1], y[2], z[0], z[1], z[2]);
  return Pose::fromRt(R, -(R * C));
}

// n cámaras en un anillo de radio `radius` alrededor del eje z, cada `step_deg` grados,
// con una pequeña variación de altura.
inline std::vector<Pose> ring(int n, double radius = 6.0, double step_deg = 30.0) {
  std::vector<Pose> poses;
  for (int k = 0; k < n; ++k) {
    const double a = k * step_deg * kPi / 180.0;
    const cv::Vec3d C(radius * std::cos(a), radius * std::sin(a), 1.5 + 0.5 * std::sin(3 * a));
    poses.push_back(lookAt(C));
  }
  return poses;
}

inline std::vector<cv::Vec3d> randomPoints(int n, std::uint64_t seed = 7, double half = 1.0) {
  cv::RNG rng(seed);
  std::vector<cv::Vec3d> pts;
  for (int k = 0; k < n; ++k)
    pts.emplace_back(rng.uniform(-half, half), rng.uniform(-half, half), rng.uniform(-half, half));
  return pts;
}

// Proyección con ruido gaussiano de desviación `noise_px` en cada coordenada.
inline cv::Point2d observe(const CameraModel& c, const Pose& p, const cv::Vec3d& X, cv::RNG& rng, double noise_px) {
  const cv::Point2d u = c.project(p, X);
  return {u.x + rng.gaussian(noise_px), u.y + rng.gaussian(noise_px)};
}

inline bool insideImage(const CameraModel& c, const cv::Point2d& u) {
  return u.x >= 0 && u.y >= 0 && u.x < c.width && u.y < c.height;
}

}  // namespace scene
