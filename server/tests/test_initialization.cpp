// Pruebas de HU-13: par inicial y pose relativa.
#include "epipolar.h"
#include "initialization.h"
#include "test_scene.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

double rotationAngleDeg(const cv::Matx33d& R) {
  const double c = std::clamp((cv::trace(R) - 1.0) / 2.0, -1.0, 1.0);
  return std::acos(c) * 180.0 / scene::kPi;
}

double angleBetweenDeg(const cv::Vec3d& a, const cv::Vec3d& b) {
  return std::acos(std::clamp(a.dot(b) / (cv::norm(a) * cv::norm(b)), -1.0, 1.0)) * 180.0 / scene::kPi;
}

// Correspondencias entre dos cámaras con ruido; E estimada con el código del Sprint 2.
PairCandidate makePair(const CameraModel& c, int i, int j, const Pose& pi, const Pose& pj, int n_points,
                       std::uint64_t seed) {
  PairCandidate p;
  p.i = i;
  p.j = j;
  cv::RNG rng(seed);
  std::vector<cv::Point2f> fi, fj;
  // Puntos en un cubo de lado 5 (llenan la imagen, como el objeto de una foto real);
  // solo se usan los que se ven en ambas cámaras.
  for (const cv::Vec3d& X : scene::randomPoints(4 * n_points, seed, 2.5)) {
    if (static_cast<int>(p.pts_i.size()) == n_points) break;
    if (pi.toCamera(X)[2] < 1.0 || pj.toCamera(X)[2] < 1.0) continue;
    const cv::Point2d a = scene::observe(c, pi, X, rng, 0.3);
    const cv::Point2d b = scene::observe(c, pj, X, rng, 0.3);
    if (!scene::insideImage(c, a) || !scene::insideImage(c, b)) continue;
    p.pts_i.push_back(a);
    p.pts_j.push_back(b);
    fi.emplace_back(a);
    fj.emplace_back(b);
  }
  const cv::Matx33d K(c.f, 0, c.cx, 0, c.f, c.cy, 0, 0, 1);
  MatchingParams mp;
  mp.ransac_threshold_px = 1.5;
  p.E = estimatePairGeometry(fi, fj, K, K, mp).E;
  return p;
}

}  // namespace

TEST(Initialization, RecoverPoseMatchesTruth) {
  const CameraModel c = scene::camera();
  const std::vector<Pose> poses = scene::ring(2, 6.0, 25.0);
  const PairCandidate pair = makePair(c, 1, 2, poses[0], poses[1], 300, 21);
  const RelativePose rel = recoverRelativePose(c, pair);
  ASSERT_TRUE(rel.ok);

  // Pose verdadera de j relativa a i: R = Rj Riᵀ, t = tj − R ti.
  const cv::Matx33d R_true = poses[1].R() * poses[0].R().t();
  const cv::Vec3d t_true = poses[1].t - R_true * poses[0].t;
  EXPECT_LT(rotationAngleDeg(rel.pose_j.R() * R_true.t()), 0.5);
  EXPECT_LT(angleBetweenDeg(rel.pose_j.t, t_true), 1.0);
  EXPECT_NEAR(cv::norm(rel.pose_j.t), 1.0, 1e-9);
  EXPECT_GT(rel.front_ratio, 0.9);
  EXPECT_GT(rel.median_angle_deg, 10.0);
}

TEST(Initialization, PrefersMostInliersWithEnoughAngle) {
  const CameraModel c = scene::camera();
  const std::vector<Pose> poses = scene::ring(3, 6.0, 25.0);
  ReconstructionParams params;
  params.init_min_inliers = 50;
  const std::vector<PairCandidate> pairs = {
      makePair(c, 1, 2, poses[0], poses[1], 150, 1),  // 25°, menos inliers
      makePair(c, 1, 3, poses[0], poses[2], 250, 2),  // 50°, más inliers -> elegido
      makePair(c, 2, 3, poses[1], poses[2], 30, 3)};  // pocos inliers
  const InitialPair init = chooseInitialPair(c, pairs, params);
  ASSERT_TRUE(init.found);
  EXPECT_EQ(init.i, 1);
  EXPECT_EQ(init.j, 3);
  ASSERT_FALSE(init.evaluated.empty());
  EXPECT_EQ(init.evaluated.front().reason, "elegido");
}

TEST(Initialization, RejectsNearZeroBaseline) {
  const CameraModel c = scene::camera();
  const Pose a = scene::lookAt({0, -6, 0});
  const Pose b = scene::lookAt({0.02, -6, 0.0}, {0.3, 0, 0});  // casi sin línea base, girada
  const std::vector<Pose> ring = scene::ring(2, 6.0, 20.0);
  ReconstructionParams params;
  params.init_min_inliers = 50;
  const std::vector<PairCandidate> pairs = {makePair(c, 1, 2, a, b, 400, 5),            // más inliers, sin base
                                            makePair(c, 3, 4, ring[0], ring[1], 200, 6)};  // válido
  const InitialPair init = chooseInitialPair(c, pairs, params);
  ASSERT_TRUE(init.found);
  EXPECT_EQ(init.i, 3);
  EXPECT_EQ(init.j, 4);
  ASSERT_EQ(init.evaluated.size(), 2u);
  EXPECT_EQ(init.evaluated[0].i, 1);
  EXPECT_NE(init.evaluated[0].reason, "elegido");
  EXPECT_LT(init.evaluated[0].median_angle_deg, params.init_min_median_triangulation_angle_deg);
}

TEST(Initialization, NoCandidateWhenAllTooSmall) {
  const CameraModel c = scene::camera();
  const std::vector<Pose> poses = scene::ring(2, 6.0, 25.0);
  const InitialPair init = chooseInitialPair(c, {makePair(c, 1, 2, poses[0], poses[1], 40, 7)}, ReconstructionParams{});
  EXPECT_FALSE(init.found);
  ASSERT_EQ(init.evaluated.size(), 1u);
  EXPECT_EQ(init.evaluated[0].reason, "pocos_inliers");
}
