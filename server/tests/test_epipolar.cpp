// Pruebas de HU-11 (F con RANSAC, E = K'ᵀ F K) y HU-12 (rechazo de pares inválidos).
#include "epipolar.h"

#include <gtest/gtest.h>
#include <opencv2/calib3d.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace {

constexpr double kPi = 3.14159265358979323846;

cv::Matx33d skew(const cv::Vec3d& t) { return {0, -t[2], t[1], t[2], 0, -t[0], -t[1], t[0], 0}; }

// Dos cámaras con K, R, t conocidos que observan puntos 3D aleatorios en un cubo.
// Convención: x2 ~ K (R X + t), x1 ~ K X.
struct SyntheticScene {
  cv::Matx33d K;
  cv::Matx33d R;
  cv::Vec3d t;
  std::vector<cv::Point2f> pts1, pts2;
  int n_true = 0;  // las primeras n_true correspondencias son verdaderas

  cv::Matx33d trueF() const { return K.inv().t() * skew(t) * R * K.inv(); }
  cv::Matx33d trueE() const { return skew(t) * R; }
};

SyntheticScene makeScene(int n_points, double noise_px, int n_outliers, uint64_t seed = 1234) {
  SyntheticScene s;
  s.K = cv::Matx33d(800, 0, 320, 0, 800, 240, 0, 0, 1);
  cv::Vec3d rvec(0.02, 0.25, -0.03);  // ≈ 14° principalmente alrededor de y
  cv::Mat R;
  cv::Rodrigues(rvec, R);
  s.R = cv::Matx33d(R);
  s.t = cv::Vec3d(-1.0, 0.1, 0.15);
  cv::RNG rng(seed);
  while (static_cast<int>(s.pts1.size()) < n_points) {
    const cv::Vec3d X(rng.uniform(-1.0, 1.0), rng.uniform(-1.0, 1.0), rng.uniform(5.0, 7.0));
    const cv::Vec3d a = s.K * X;
    const cv::Vec3d b = s.K * (s.R * X + s.t);
    const cv::Point2f p1(float(a[0] / a[2] + rng.gaussian(noise_px)), float(a[1] / a[2] + rng.gaussian(noise_px)));
    const cv::Point2f p2(float(b[0] / b[2] + rng.gaussian(noise_px)), float(b[1] / b[2] + rng.gaussian(noise_px)));
    if (p1.x < 0 || p1.x > 640 || p1.y < 0 || p1.y > 480 || p2.x < 0 || p2.x > 640 || p2.y < 0 || p2.y > 480)
      continue;
    s.pts1.push_back(p1);
    s.pts2.push_back(p2);
  }
  s.n_true = n_points;
  for (int k = 0; k < n_outliers; ++k) {
    s.pts1.emplace_back(rng.uniform(0.f, 640.f), rng.uniform(0.f, 480.f));
    s.pts2.emplace_back(rng.uniform(0.f, 640.f), rng.uniform(0.f, 480.f));
  }
  return s;
}

cv::Vec3d singularValues(const cv::Matx33d& m) {
  cv::Mat w;
  cv::SVD::compute(cv::Mat(m), w);
  return {w.at<double>(0), w.at<double>(1), w.at<double>(2)};
}

// Distancia entre dos matrices definidas salvo escala y signo.
double projectiveDistance(const cv::Matx33d& a, const cv::Matx33d& b) {
  const cv::Matx33d an = a * (1.0 / cv::norm(a));
  const cv::Matx33d bn = b * (1.0 / cv::norm(b));
  return std::min(cv::norm(an - bn), cv::norm(an + bn));
}

const MatchingParams kDefaults{};

}  // namespace

// ---------- HU-11 ----------

TEST(Sampson, HandComputedCase) {
  // F = [t]x con t = (1, 0, 0): traslación pura en x con K = I.
  const cv::Matx33d F = skew({1, 0, 0});
  // x'ᵀ F x = v - v' = -3;  (Fx)₁² + (Fx)₂² + (Fᵀx')₁² + (Fᵀx')₂² = 0 + 1 + 0 + 1 = 2
  EXPECT_NEAR(sampsonDistance(F, {10, 20}, {30, 23}), 3.0 / std::sqrt(2.0), 1e-12);
  EXPECT_NEAR(sampsonDistance(F, {10, 20}, {-50, 20}), 0.0, 1e-12);  // misma fila: sobre la epipolar
}

TEST(Sampson, AnotherHandComputedCase) {
  const cv::Matx33d F(1, 2, 3, 4, 5, 6, 7, 8, 10);
  // x = (1, 2, 1), x' = (3, 1, 1):  Fx = (8, 20, 33), Fᵀx' = (14, 19, 25), x'ᵀFx = 24 + 20 + 33 = 77
  const double expected = 77.0 / std::sqrt(8.0 * 8 + 20.0 * 20 + 14.0 * 14 + 19.0 * 19);
  EXPECT_NEAR(sampsonDistance(F, {1, 2}, {3, 1}), expected, 1e-12);
}

TEST(Epipolar, TrueFundamentalGivesSmallSampsonError) {
  const SyntheticScene s = makeScene(200, 0.3, 0);
  double sum = 0;
  for (int k = 0; k < s.n_true; ++k) sum += sampsonDistance(s.trueF(), s.pts1[k], s.pts2[k]);
  EXPECT_LT(sum / s.n_true, 0.5);
}

TEST(Epipolar, EstimatedFHasRank2AndSmallSampsonError) {
  const SyntheticScene s = makeScene(200, 0.3, 0);
  const PairGeometry g = estimatePairGeometry(s.pts1, s.pts2, s.K, s.K, kDefaults);
  ASSERT_TRUE(g.f_estimated);
  const cv::Vec3d sv = singularValues(g.F);
  EXPECT_LT(sv[2], 1e-9 * sv[0]);  // rango 2
  EXPECT_GE(g.n_inliers, 190);
  EXPECT_LT(g.mean_sampson_px, 0.5);
  for (int idx : g.inliers) EXPECT_LE(sampsonDistance(g.F, s.pts1[idx], s.pts2[idx]), 1.0);
  EXPECT_LT(projectiveDistance(g.F, s.trueF()), 0.05);
}

TEST(Epipolar, EssentialIsKtFKProjectedToEssentialManifold) {
  const SyntheticScene s = makeScene(200, 0.3, 0);
  const PairGeometry g = estimatePairGeometry(s.pts1, s.pts2, s.K, s.K, kDefaults);
  ASSERT_TRUE(g.f_estimated);
  const cv::Vec3d sv = singularValues(g.E);
  EXPECT_NEAR(sv[0], sv[1], 1e-9 * sv[0]);  // dos valores singulares iguales
  EXPECT_LT(sv[2], 1e-9 * sv[0]);           // y el tercero 0
  // Con ruido de 0,3 px y f = 800 px, K amplifica el error de F: E queda a ~0,09 de la verdadera.
  EXPECT_LT(projectiveDistance(g.E, s.trueE()), 0.2);
}

TEST(Epipolar, EssentialMatchesTrueEssentialWithoutNoise) {
  const SyntheticScene s = makeScene(200, 0.0, 0);
  const PairGeometry g = estimatePairGeometry(s.pts1, s.pts2, s.K, s.K, kDefaults);
  ASSERT_TRUE(g.f_estimated);
  EXPECT_EQ(g.n_inliers, 200);
  EXPECT_LT(projectiveDistance(g.E, s.trueE()), 1e-3);
  EXPECT_LT(projectiveDistance(s.K.t() * g.F * s.K, s.trueE()), 1e-3);
}

TEST(Epipolar, EssentialUsesEachViewIntrinsics) {
  const cv::Matx33d F(0, -1e-6, 1e-3, 1e-6, 0, -2e-3, -1e-3, 2e-3, 1);
  const cv::Matx33d K1(800, 0, 320, 0, 800, 240, 0, 0, 1);
  const cv::Matx33d K2(900, 0, 300, 0, 900, 250, 0, 0, 1);
  const cv::Matx33d E = essentialFromFundamental(F, K1, K2);
  const cv::Matx33d raw = K2.t() * F * K1;
  const cv::Vec3d sv_raw = singularValues(raw);
  const cv::Vec3d sv = singularValues(E);
  EXPECT_NEAR(sv[0], (sv_raw[0] + sv_raw[1]) / 2, 1e-9 * sv_raw[0]);
  EXPECT_NEAR(sv[1], (sv_raw[0] + sv_raw[1]) / 2, 1e-9 * sv_raw[0]);
  EXPECT_LT(sv[2], 1e-9 * sv_raw[0]);
}

TEST(Epipolar, RansacRecoversInliersWith40PercentOutliers) {
  // 200 verdaderas + 134 aleatorias: el 40 % del total son falsas.
  const SyntheticScene s = makeScene(200, 0.3, 134);
  const PairGeometry g = estimatePairGeometry(s.pts1, s.pts2, s.K, s.K, kDefaults);
  ASSERT_TRUE(g.f_estimated);
  const std::set<int> found(g.inliers.begin(), g.inliers.end());
  int recovered = 0;
  for (int k = 0; k < s.n_true; ++k) recovered += found.count(k) ? 1 : 0;
  EXPECT_GE(recovered, 180) << "verdaderas recuperadas: " << recovered << " de 200";
  int false_inliers = static_cast<int>(found.size()) - recovered;
  EXPECT_LT(false_inliers, 15);
  EXPECT_EQ(g.n_matches, 334);
  EXPECT_NEAR(g.inlier_ratio, static_cast<double>(g.n_inliers) / 334, 1e-12);
}

TEST(Epipolar, EnforceRank2ZeroesSmallestSingularValue) {
  const cv::Matx33d F(1, 2, 3, 4, 5, 6, 7, 8, 10);  // rango 3
  const cv::Vec3d before = singularValues(F);
  const cv::Vec3d after = singularValues(enforceRank2(F));
  EXPECT_NEAR(after[0], before[0], 1e-9);
  EXPECT_NEAR(after[1], before[1], 1e-9);
  EXPECT_LT(after[2], 1e-12);
}
