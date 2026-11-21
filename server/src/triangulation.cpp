#include "triangulation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

}  // namespace

cv::Vec4d triangulateHomogeneous(const std::vector<Pose>& poses, const std::vector<cv::Point2d>& normalized) {
  if (poses.size() != normalized.size() || poses.size() < 2)
    throw std::invalid_argument("la triangulación necesita al menos 2 vistas");
  cv::Mat A(static_cast<int>(2 * poses.size()), 4, CV_64F);
  for (std::size_t k = 0; k < poses.size(); ++k) {
    const cv::Matx34d P = poses[k].matrix();
    const cv::Point2d& x = normalized[k];
    for (int r = 0; r < 2; ++r) {
      const double coord = r == 0 ? x.x : x.y;
      double* row = A.ptr<double>(static_cast<int>(2 * k) + r);
      double norm2 = 0;
      for (int c = 0; c < 4; ++c) {
        row[c] = coord * P(2, c) - P(r, c);
        norm2 += row[c] * row[c];
      }
      // Cada fila se normaliza para que todas las vistas pesen igual.
      const double n = std::sqrt(norm2);
      if (n > 0)
        for (int c = 0; c < 4; ++c) row[c] /= n;
    }
  }
  cv::Mat w, u, vt;
  cv::SVD::compute(A, w, u, vt, cv::SVD::FULL_UV);
  return {vt.at<double>(3, 0), vt.at<double>(3, 1), vt.at<double>(3, 2), vt.at<double>(3, 3)};
}

cv::Vec3d triangulateDLT(const std::vector<Pose>& poses, const std::vector<cv::Point2d>& normalized) {
  const cv::Vec4d Xh = triangulateHomogeneous(poses, normalized);
  if (std::abs(Xh[3]) < 1e-12) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    return {nan, nan, nan};
  }
  return {Xh[0] / Xh[3], Xh[1] / Xh[3], Xh[2] / Xh[3]};
}

double triangulationAngleDeg(const cv::Vec3d& X, const cv::Vec3d& c1, const cv::Vec3d& c2) {
  const cv::Vec3d r1 = c1 - X;
  const cv::Vec3d r2 = c2 - X;
  const double n = cv::norm(r1) * cv::norm(r2);
  if (!(n > 0)) return 0;
  const double cosine = std::clamp(r1.dot(r2) / n, -1.0, 1.0);
  return std::acos(cosine) * kRadToDeg;
}

double maxTriangulationAngleDeg(const cv::Vec3d& X, const std::vector<cv::Vec3d>& centers) {
  double best = 0;
  for (std::size_t a = 0; a < centers.size(); ++a)
    for (std::size_t b = a + 1; b < centers.size(); ++b)
      best = std::max(best, triangulationAngleDeg(X, centers[a], centers[b]));
  return best;
}

TriangulationResult triangulateChecked(const CameraModel& camera, const std::vector<Pose>& poses,
                                       const std::vector<cv::Point2d>& pixels, double min_angle_deg,
                                       double max_error_px) {
  TriangulationResult r;
  std::vector<cv::Point2d> normalized;
  normalized.reserve(pixels.size());
  for (const cv::Point2d& p : pixels) normalized.push_back(camera.pixelToNormalized(p));
  r.X = triangulateDLT(poses, normalized);
  if (!std::isfinite(r.X[0])) {
    r.reason = "angulo_pequeno";  // rayos paralelos: punto en el infinito
    return r;
  }

  std::vector<cv::Vec3d> centers;
  for (const Pose& p : poses) {
    if (p.toCamera(r.X)[2] <= 0) {
      r.reason = "detras_de_camara";
      return r;
    }
    centers.push_back(p.center());
  }
  r.angle_deg = maxTriangulationAngleDeg(r.X, centers);
  for (std::size_t k = 0; k < poses.size(); ++k)
    r.max_error_px = std::max(r.max_error_px, reprojectionError(camera, poses[k], r.X, pixels[k]));
  if (r.angle_deg < min_angle_deg) {
    r.reason = "angulo_pequeno";
    return r;
  }
  if (r.max_error_px > max_error_px) {
    r.reason = "error_alto";
    return r;
  }
  r.ok = true;
  return r;
}
