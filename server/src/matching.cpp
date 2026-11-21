#include "matching.h"

#include <opencv2/features2d.hpp>

std::vector<FeatureMatch> matchFeatures(const ImageFeatures& a, const ImageFeatures& b, double ratio) {
  std::vector<FeatureMatch> out;
  if (a.descriptors.rows < 1 || b.descriptors.rows < 2) return out;  // el ratio test necesita 2 vecinos

  cv::BFMatcher matcher(cv::NORM_L2, /*crossCheck=*/false);
  std::vector<std::vector<cv::DMatch>> knn;
  matcher.knnMatch(a.descriptors, b.descriptors, knn, 2);

  out.reserve(knn.size());
  for (const std::vector<cv::DMatch>& m : knn) {
    if (m.size() < 2) continue;
    // Estricto (<): si el primer y el segundo vecino son igual de parecidos, la correspondencia es ambigua.
    if (!(m[0].distance < ratio * m[1].distance)) continue;
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
