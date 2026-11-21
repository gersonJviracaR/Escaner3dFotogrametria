#include "camera_model.h"

#include <cmath>
#include <stdexcept>

double quaternionNorm(const Quat& q) { return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]); }

cv::Matx33d quaternionToRotation(const Quat& q_in) {
  const double n = quaternionNorm(q_in);
  if (!(n > 0)) throw std::invalid_argument("cuaternión de norma 0");
  const double w = q_in[0] / n, x = q_in[1] / n, y = q_in[2] / n, z = q_in[3] / n;
  return {1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),  //
          2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),  //
          2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)};
}

Quat rotationToQuaternion(const cv::Matx33d& R) {
  // Método de Shepperd: se elige la rama numéricamente más estable.
  Quat q;
  const double trace = R(0, 0) + R(1, 1) + R(2, 2);
  if (trace > 0) {
    const double s = std::sqrt(trace + 1.0) * 2;
    q = {0.25 * s, (R(2, 1) - R(1, 2)) / s, (R(0, 2) - R(2, 0)) / s, (R(1, 0) - R(0, 1)) / s};
  } else if (R(0, 0) > R(1, 1) && R(0, 0) > R(2, 2)) {
    const double s = std::sqrt(1.0 + R(0, 0) - R(1, 1) - R(2, 2)) * 2;
    q = {(R(2, 1) - R(1, 2)) / s, 0.25 * s, (R(0, 1) + R(1, 0)) / s, (R(0, 2) + R(2, 0)) / s};
  } else if (R(1, 1) > R(2, 2)) {
    const double s = std::sqrt(1.0 + R(1, 1) - R(0, 0) - R(2, 2)) * 2;
    q = {(R(0, 2) - R(2, 0)) / s, (R(0, 1) + R(1, 0)) / s, 0.25 * s, (R(1, 2) + R(2, 1)) / s};
  } else {
    const double s = std::sqrt(1.0 + R(2, 2) - R(0, 0) - R(1, 1)) * 2;
    q = {(R(1, 0) - R(0, 1)) / s, (R(0, 2) + R(2, 0)) / s, (R(1, 2) + R(2, 1)) / s, 0.25 * s};
  }
  const double n = quaternionNorm(q);
  const double sign = q[0] < 0 ? -1.0 : 1.0;
  for (double& v : q) v = sign * v / n;
  return q;
}

Pose Pose::fromRt(const cv::Matx33d& R, const cv::Vec3d& t) {
  Pose p;
  p.q = rotationToQuaternion(R);
  p.t = t;
  return p;
}

cv::Matx33d Pose::R() const { return quaternionToRotation(q); }

cv::Vec3d Pose::center() const { return -(R().t() * t); }

cv::Vec3d Pose::toCamera(const cv::Vec3d& X) const { return R() * X + t; }

cv::Matx34d Pose::matrix() const {
  const cv::Matx33d r = R();
  return {r(0, 0), r(0, 1), r(0, 2), t[0],  //
          r(1, 0), r(1, 1), r(1, 2), t[1],  //
          r(2, 0), r(2, 1), r(2, 2), t[2]};
}

cv::Point2d CameraModel::projectCamera(const cv::Vec3d& x_cam) const {
  const double a = x_cam[0] / x_cam[2];
  const double b = x_cam[1] / x_cam[2];
  const double r2 = a * a + b * b;
  const double d = 1.0 + k1 * r2 + k2 * r2 * r2;
  return {f * d * a + cx, f * d * b + cy};
}

cv::Point2d CameraModel::project(const Pose& pose, const cv::Vec3d& X) const {
  return projectCamera(pose.toCamera(X));
}

cv::Point2d CameraModel::pixelToNormalized(const cv::Point2d& pixel) const {
  const double xd = (pixel.x - cx) / f;
  const double yd = (pixel.y - cy) / f;
  if (k1 == 0 && k2 == 0) return {xd, yd};
  // Punto fijo a = xd / d(a, b): converge rápido para la distorsión moderada de un celular.
  double a = xd, b = yd;
  for (int it = 0; it < 100; ++it) {
    const double r2 = a * a + b * b;
    const double d = 1.0 + k1 * r2 + k2 * r2 * r2;
    const double na = xd / d, nb = yd / d;
    const double change = std::abs(na - a) + std::abs(nb - b);
    a = na;
    b = nb;
    if (change < 1e-15) break;
  }
  return {a, b};
}

double reprojectionError(const CameraModel& camera, const Pose& pose, const cv::Vec3d& X, const cv::Point2d& observed) {
  const cv::Point2d u = camera.project(pose, X);
  return std::hypot(u.x - observed.x, u.y - observed.y);
}
