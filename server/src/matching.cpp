#include "matching.h"

#include "epipolar.h"

#include <opencv2/features2d.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

std::vector<FeatureMatch> matchFeatures(const ImageFeatures& a, const ImageFeatures& b, double ratio,
                                        bool mutual_check) {
  std::vector<FeatureMatch> out;
  if (a.descriptors.rows < 1 || b.descriptors.rows < 2) return out;  // el ratio test necesita 2 vecinos

  cv::BFMatcher matcher(cv::NORM_L2, /*crossCheck=*/false);
  std::vector<std::vector<cv::DMatch>> knn;
  matcher.knnMatch(a.descriptors, b.descriptors, knn, 2);
  std::vector<int> best_in_a;  // para mutual_check: vecino más cercano en A de cada punto de B
  if (mutual_check) {
    std::vector<cv::DMatch> back;
    matcher.match(b.descriptors, a.descriptors, back);
    best_in_a.assign(static_cast<std::size_t>(b.descriptors.rows), -1);
    for (const cv::DMatch& m : back) best_in_a[static_cast<std::size_t>(m.queryIdx)] = m.trainIdx;
  }

  out.reserve(knn.size());
  for (const std::vector<cv::DMatch>& m : knn) {
    if (m.size() < 2) continue;
    // Estricto (<): si el primer y el segundo vecino son igual de parecidos, la correspondencia es ambigua.
    if (!(m[0].distance < ratio * m[1].distance)) continue;
    if (mutual_check && best_in_a[static_cast<std::size_t>(m[0].trainIdx)] != m[0].queryIdx) continue;
    FeatureMatch fm;
    fm.index_a = m[0].queryIdx;
    fm.index_b = m[0].trainIdx;
    fm.point_a = a.keypoints[fm.index_a].pt;
    fm.point_b = b.keypoints[fm.index_b].pt;
    fm.distance = m[0].distance;
    out.push_back(fm);
  }
  return out;
}

namespace {

// Para cada punto de A, el mejor de B dentro de la banda epipolar (índice o -1) y su ratio test.
struct BandBest {
  int index = -1;
  float distance = 0;
  bool passes_ratio = false;
};

std::vector<BandBest> bestInBand(const ImageFeatures& a, const ImageFeatures& b, const cv::Matx33d& F,
                                 double max_sampson_px, double ratio) {
  std::vector<BandBest> out(a.keypoints.size());
  const int nb = static_cast<int>(b.keypoints.size());
  for (int ia = 0; ia < static_cast<int>(a.keypoints.size()); ++ia) {
    const cv::Point2f pa = a.keypoints[static_cast<std::size_t>(ia)].pt;
    const cv::Vec3d l = F * cv::Vec3d(pa.x, pa.y, 1.0);  // línea epipolar en B
    const double ln = std::sqrt(l[0] * l[0] + l[1] * l[1]);
    if (!(ln > 0)) continue;
    float d1 = std::numeric_limits<float>::max(), d2 = d1;
    int best = -1;
    for (int ib = 0; ib < nb; ++ib) {
      const cv::Point2f pb = b.keypoints[static_cast<std::size_t>(ib)].pt;
      // Filtro rápido: la distancia de Sampson nunca supera la distancia a la línea.
      if (std::abs(l[0] * pb.x + l[1] * pb.y + l[2]) / ln > 2 * max_sampson_px) continue;
      if (sampsonDistance(F, pa, pb) > max_sampson_px) continue;
      const float d = static_cast<float>(cv::norm(a.descriptors.row(ia), b.descriptors.row(ib), cv::NORM_L2));
      if (d < d1) {
        d2 = d1;
        d1 = d;
        best = ib;
      } else if (d < d2) {
        d2 = d;
      }
    }
    BandBest& r = out[static_cast<std::size_t>(ia)];
    r.index = best;
    r.distance = d1;
    r.passes_ratio = best >= 0 && d2 != std::numeric_limits<float>::max() && d1 < ratio * d2;
  }
  return out;
}

}  // namespace

std::vector<FeatureMatch> matchFeaturesGuided(const ImageFeatures& a, const ImageFeatures& b, const cv::Matx33d& F,
                                              double max_sampson_px, double ratio, bool mutual_check) {
  const std::vector<BandBest> ab = bestInBand(a, b, F, max_sampson_px, ratio);
  std::vector<BandBest> ba;
  if (mutual_check) ba = bestInBand(b, a, F.t(), max_sampson_px, ratio);
  std::map<int, FeatureMatch> best_for_b;  // índice en B -> mejor correspondencia
  for (int ia = 0; ia < static_cast<int>(ab.size()); ++ia) {
    const BandBest& r = ab[static_cast<std::size_t>(ia)];
    if (!r.passes_ratio) continue;
    if (mutual_check) {
      const BandBest& back = ba[static_cast<std::size_t>(r.index)];
      if (!back.passes_ratio || back.index != ia) continue;
    }
    FeatureMatch m{ia, r.index, a.keypoints[static_cast<std::size_t>(ia)].pt,
                   b.keypoints[static_cast<std::size_t>(r.index)].pt, r.distance};
    const auto it = best_for_b.find(r.index);
    if (it == best_for_b.end() || m.distance < it->second.distance) best_for_b[r.index] = m;
  }
  std::vector<FeatureMatch> out;
  out.reserve(best_for_b.size());
  for (const auto& [ib, m] : best_for_b) out.push_back(m);
  std::sort(out.begin(), out.end(), [](const FeatureMatch& x, const FeatureMatch& y) { return x.index_a < y.index_a; });
  return out;
}

