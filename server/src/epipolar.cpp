#include "epipolar.h"

#include <opencv2/calib3d.hpp>

#include <cmath>
#include <limits>

namespace {

// Por debajo de esto (σ₃ / σ₁) la F de OpenCV ya se considera de rango 2.
constexpr double kRank2Tolerance = 1e-12;

}  // namespace

double sampsonDistance(const cv::Matx33d& F, const cv::Point2d& x, const cv::Point2d& x_prime) {
  const cv::Vec3d p(x.x, x.y, 1.0);
  const cv::Vec3d q(x_prime.x, x_prime.y, 1.0);
  const cv::Vec3d Fp = F * p;
  const cv::Vec3d Ftq = F.t() * q;
  const double algebraic = q.dot(Fp);
  const double denom = Fp[0] * Fp[0] + Fp[1] * Fp[1] + Ftq[0] * Ftq[0] + Ftq[1] * Ftq[1];
  if (algebraic == 0) return 0;
  if (!(denom > 0)) return std::numeric_limits<double>::infinity();
  return std::abs(algebraic) / std::sqrt(denom);
}

cv::Matx33d enforceRank2(const cv::Matx33d& F) {
  cv::Matx33d U, Vt;
  cv::Matx31d w;
  cv::SVD::compute(F, w, U, Vt);
  return U * cv::Matx33d::diag({w(0), w(1), 0.0}) * Vt;
}

cv::Matx33d essentialFromFundamental(const cv::Matx33d& F, const cv::Matx33d& K, const cv::Matx33d& K_prime) {
  const cv::Matx33d raw = K_prime.t() * F * K;
  cv::Matx33d U, Vt;
  cv::Matx31d w;
  cv::SVD::compute(raw, w, U, Vt);
  const double s = (w(0) + w(1)) / 2.0;
  return U * cv::Matx33d::diag({s, s, 0.0}) * Vt;
}

std::vector<std::string> rejectionReasons(bool f_estimated, int n_inliers, double inlier_ratio,
                                          const MatchingParams& params) {
  std::vector<std::string> reasons;
  if (!f_estimated) reasons.emplace_back("f_no_estimada");
  if (n_inliers < params.min_inliers) reasons.emplace_back("pocos_inliers");
  if (inlier_ratio < params.min_inlier_ratio) reasons.emplace_back("razon_inliers_baja");
  return reasons;
}

namespace {

// F, E e inliers; deja f_estimated = false si F no se pudo estimar.
void fitGeometry(PairGeometry& g, const std::vector<cv::Point2f>& pts_i, const std::vector<cv::Point2f>& pts_j,
                 const cv::Matx33d& K_i, const cv::Matx33d& K_j, const MatchingParams& params) {
  // Con 7 puntos el algoritmo de 7 puntos da hasta 3 soluciones: se exige al menos 8.
  if (pts_i.size() != pts_j.size() || pts_i.size() < 8) return;

  cv::Mat F_cv = cv::findFundamentalMat(pts_i, pts_j, cv::FM_RANSAC, params.ransac_threshold_px,
                                        params.ransac_confidence, kRansacMaxIters, cv::noArray());
  if (F_cv.rows != 3 || F_cv.cols != 3) return;
  cv::Matx33d F;
  F_cv.convertTo(cv::Mat(F, false), CV_64F);
  if (!cv::checkRange(cv::Mat(F, false))) return;

  cv::Matx31d w;
  cv::SVD::compute(F, w);
  g.rank2_enforced = w(2) > kRank2Tolerance * w(0);
  g.F = enforceRank2(F);  // siempre se proyecta: inofensivo si ya era de rango 2
  g.E = essentialFromFundamental(g.F, K_i, K_j);
  g.f_estimated = true;

  // Inliers con la distancia de Sampson propia (no la máscara de OpenCV, que usa otra medida).
  double sum = 0;
  for (std::size_t k = 0; k < pts_i.size(); ++k) {
    const double d = sampsonDistance(g.F, pts_i[k], pts_j[k]);
    if (d <= params.ransac_threshold_px) {
      g.inliers.push_back(static_cast<int>(k));
      sum += d;
    }
  }
  g.n_inliers = static_cast<int>(g.inliers.size());
  g.inlier_ratio = static_cast<double>(g.n_inliers) / g.n_matches;
  g.mean_sampson_px = g.n_inliers > 0 ? sum / g.n_inliers : 0.0;
}

}  // namespace

PairGeometry estimatePairGeometry(const std::vector<cv::Point2f>& pts_i, const std::vector<cv::Point2f>& pts_j,
                                  const cv::Matx33d& K_i, const cv::Matx33d& K_j, const MatchingParams& params) {
  PairGeometry g;
  g.n_matches = static_cast<int>(pts_i.size());
  fitGeometry(g, pts_i, pts_j, K_i, K_j, params);
  g.reasons = rejectionReasons(g.f_estimated, g.n_inliers, g.inlier_ratio, params);
  g.accepted = g.reasons.empty();
  return g;
}
