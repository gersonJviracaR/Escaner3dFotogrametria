// Pruebas de HU-09: emparejamiento de descriptores con ratio test.
#include "features.h"
#include "matching.h"
#include "test_images.h"

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

#include <cmath>

namespace {

// Segunda "vista": rotación, escala y traslación conocidas de la misma textura.
struct WarpedPair {
  cv::Mat a, b;
  cv::Matx23d transform;  // p_b = transform * [p_a; 1]
};

WarpedPair warpedPair(double angle_deg, double scale, cv::Point2d shift) {
  WarpedPair w;
  w.a = testimg::blobTexture(800, 600, 21);
  cv::Mat m = cv::getRotationMatrix2D(cv::Point2f(400, 300), angle_deg, scale);
  m.at<double>(0, 2) += shift.x;
  m.at<double>(1, 2) += shift.y;
  w.transform = cv::Matx23d(m);
  cv::warpAffine(w.a, w.b, m, w.a.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(128));
  return w;
}

double transferError(const WarpedPair& w, const FeatureMatch& m) {
  const cv::Vec3d pa(m.point_a.x, m.point_a.y, 1.0);
  const cv::Vec2d expected = w.transform * pa;
  return std::hypot(expected[0] - m.point_b.x, expected[1] - m.point_b.y);
}

// Mosaico de una misma tesela: cada punto tiene copias idénticas (ambigüedad total).
cv::Mat tiledTexture() {
  const cv::Mat tile = testimg::blobTexture(80, 80, 4);
  cv::Mat img(480, 640, CV_8UC1);
  for (int y = 0; y < img.rows; y += tile.rows)
    for (int x = 0; x < img.cols; x += tile.cols) tile.copyTo(img(cv::Rect(x, y, tile.cols, tile.rows)));
  return img;
}

}  // namespace

TEST(Matching, MatchesFollowKnownTransform) {
  const WarpedPair w = warpedPair(20.0, 0.85, {15, -10});
  const MatchingParams p;
  const ImageFeatures fa = extractFeatures(w.a, p);
  const ImageFeatures fb = extractFeatures(w.b, p);
  const std::vector<FeatureMatch> matches = matchFeatures(fa, fb, p.ratio);
  ASSERT_GE(matches.size(), 200u);
  int consistent = 0;
  for (const FeatureMatch& m : matches)
    if (transferError(w, m) <= 3.0) ++consistent;
  EXPECT_GE(static_cast<double>(consistent) / matches.size(), 0.9)
      << consistent << " de " << matches.size() << " correspondencias coinciden con la transformación";
}

TEST(Matching, EachMatchCarriesIndicesPointsAndDistance) {
  const WarpedPair w = warpedPair(10.0, 1.0, {5, 5});
  const MatchingParams p;
  const ImageFeatures fa = extractFeatures(w.a, p);
  const ImageFeatures fb = extractFeatures(w.b, p);
  const std::vector<FeatureMatch> matches = matchFeatures(fa, fb, p.ratio);
  ASSERT_FALSE(matches.empty());
  for (const FeatureMatch& m : matches) {
    ASSERT_GE(m.index_a, 0);
    ASSERT_LT(m.index_a, static_cast<int>(fa.keypoints.size()));
    ASSERT_GE(m.index_b, 0);
    ASSERT_LT(m.index_b, static_cast<int>(fb.keypoints.size()));
    EXPECT_EQ(m.point_a, fa.keypoints[m.index_a].pt);
    EXPECT_EQ(m.point_b, fb.keypoints[m.index_b].pt);
    const double d = cv::norm(fa.descriptors.row(m.index_a), fb.descriptors.row(m.index_b), cv::NORM_L2);
    EXPECT_NEAR(m.distance, d, 1e-3 * std::max(1.0, d));
  }
}

TEST(Matching, RatioTestDiscardsAmbiguousRepetitiveTexture) {
  const MatchingParams p;
  const ImageFeatures tiled = extractFeatures(tiledTexture(), p);
  const ImageFeatures unique = extractFeatures(testimg::blobTexture(640, 480, 4), p);
  ASSERT_GT(tiled.keypoints.size(), 100u);
  ASSERT_GT(unique.keypoints.size(), 100u);

  // Contra sí misma, la textura única se empareja casi entera; la repetitiva no,
  // porque el primer y el segundo vecino son copias igual de parecidas.
  const double kept_tiled =
      static_cast<double>(matchFeatures(tiled, tiled, p.ratio).size()) / tiled.keypoints.size();
  const double kept_unique =
      static_cast<double>(matchFeatures(unique, unique, p.ratio).size()) / unique.keypoints.size();
  EXPECT_LT(kept_tiled, 0.2);
  EXPECT_GT(kept_unique, 0.8);
}

TEST(Matching, StricterRatioKeepsFewerMatches) {
  const WarpedPair w = warpedPair(30.0, 0.7, {0, 0});
  const MatchingParams p;
  const ImageFeatures fa = extractFeatures(w.a, p);
  const ImageFeatures fb = extractFeatures(w.b, p);
  const auto loose = matchFeatures(fa, fb, 0.9);
  const auto strict = matchFeatures(fa, fb, 0.6);
  EXPECT_LT(strict.size(), loose.size());
  for (const FeatureMatch& m : strict) EXPECT_GE(m.distance, 0.0f);
}

TEST(Matching, TooFewDescriptorsGiveNoMatches) {
  const ImageFeatures fa = extractFeatures(testimg::blobTexture(320, 240, 1), MatchingParams{});
  ImageFeatures single = fa;
  single.keypoints.resize(1);
  single.descriptors = fa.descriptors.row(0).clone();
  EXPECT_TRUE(matchFeatures(fa, single, 0.75).empty());  // sin segundo vecino no hay ratio test
  EXPECT_TRUE(matchFeatures(ImageFeatures{}, fa, 0.75).empty());
}
