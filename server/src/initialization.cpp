#include "initialization.h"

#include "triangulation.h"

#include <opencv2/calib3d.hpp>

#include <algorithm>
#include <numeric>

RelativePose recoverRelativePose(const CameraModel& camera, const PairCandidate& pair) {
  RelativePose rel;
  rel.n_points = pair.n_inliers();
  if (rel.n_points < 5 || pair.pts_j.size() != pair.pts_i.size()) return rel;

  std::vector<cv::Point2d> ni, nj;
  ni.reserve(pair.pts_i.size());
  nj.reserve(pair.pts_j.size());
  for (std::size_t k = 0; k < pair.pts_i.size(); ++k) {
    ni.push_back(camera.pixelToNormalized(pair.pts_i[k]));
    nj.push_back(camera.pixelToNormalized(pair.pts_j[k]));
  }
  // Con coordenadas normalizadas, la matriz de cámara de recoverPose es la identidad.
  cv::Mat R, t;
  const int passed = cv::recoverPose(cv::Mat(pair.E), ni, nj, cv::Mat::eye(3, 3, CV_64F), R, t);
  if (passed <= 0 || R.empty() || t.empty()) return rel;
  const cv::Vec3d tv(t.at<double>(0), t.at<double>(1), t.at<double>(2));
  rel.pose_j = Pose::fromRt(cv::Matx33d(R), tv * (1.0 / cv::norm(tv)));  // |t| = 1 fija la escala

  const Pose origin;
  const std::vector<Pose> poses = {origin, rel.pose_j};
  const std::vector<cv::Vec3d> centers = {origin.center(), rel.pose_j.center()};
  std::vector<double> angles;
  for (std::size_t k = 0; k < ni.size(); ++k) {
    const cv::Vec3d X = triangulateDLT(poses, {ni[k], nj[k]});
    if (!std::isfinite(X[0]) || origin.toCamera(X)[2] <= 0 || rel.pose_j.toCamera(X)[2] <= 0) continue;
    angles.push_back(triangulationAngleDeg(X, centers[0], centers[1]));
  }
  rel.n_front = static_cast<int>(angles.size());
  rel.front_ratio = static_cast<double>(rel.n_front) / rel.n_points;
  if (!angles.empty()) {
    std::nth_element(angles.begin(), angles.begin() + angles.size() / 2, angles.end());
    rel.median_angle_deg = angles[angles.size() / 2];
  }
  rel.ok = true;
  return rel;
}

InitialPair chooseInitialPair(const CameraModel& camera, const std::vector<PairCandidate>& pairs,
                              const ReconstructionParams& params) {
  InitialPair init;
  std::vector<std::size_t> order(pairs.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::stable_sort(order.begin(), order.end(),
                   [&](std::size_t a, std::size_t b) { return pairs[a].n_inliers() > pairs[b].n_inliers(); });

  for (std::size_t k : order) {
    const PairCandidate& p = pairs[k];
    InitialPairChoice c;
    c.i = p.i;
    c.j = p.j;
    c.n_inliers = p.n_inliers();
    if (c.n_inliers < params.init_min_inliers) {
      c.reason = "pocos_inliers";
      init.evaluated.push_back(c);
      continue;  // los siguientes tienen aún menos, pero se registran para el reporte
    }
    const RelativePose rel = recoverRelativePose(camera, p);
    c.front_ratio = rel.front_ratio;
    c.median_angle_deg = rel.median_angle_deg;
    if (!rel.ok || rel.front_ratio <= 0.5) {
      c.reason = "pocos_puntos_delante";
    } else if (rel.median_angle_deg < params.init_min_median_triangulation_angle_deg) {
      c.reason = "angulo_mediano_bajo";
    } else {
      c.reason = "elegido";
      init.evaluated.push_back(c);
      init.found = true;
      init.i = p.i;
      init.j = p.j;
      init.relative = rel;
      return init;
    }
    init.evaluated.push_back(c);
  }
  return init;
}
