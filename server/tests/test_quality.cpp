// Pruebas de HU-06: detección de fotos borrosas o saturadas.
#include "quality.h"

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace {

// Tablero de ajedrez con casillas proporcionales al tamaño (misma "escena"
// a cualquier resolución), en gris medio para que no esté saturado.
cv::Mat makeCheckerboard(int width, int height) {
  cv::Mat img(height, width, CV_8UC3);
  const int square = std::max(1, width / 20);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const uchar v = ((x / square + y / square) % 2) ? 200 : 40;
      img.at<cv::Vec3b>(y, x) = cv::Vec3b(v, v, v);
    }
  return img;
}

cv::Mat makeNoise(int width, int height) {
  cv::Mat img(height, width, CV_8UC1);
  cv::RNG rng(42);
  rng.fill(img, cv::RNG::UNIFORM, 20, 230);
  return img;
}

cv::Mat strongBlur(const cv::Mat& img) {
  cv::Mat blurred;
  cv::GaussianBlur(img, blurred, cv::Size(0, 0), 15.0);
  return blurred;
}

const QualityThresholds kDefaults{};

}  // namespace

TEST(Quality, SharpCheckerboardIsAccepted) {
  const QualityResult r = evaluateQuality(makeCheckerboard(1600, 1200), kDefaults);
  EXPECT_TRUE(r.accepted);
  EXPECT_TRUE(r.reasons.empty());
  EXPECT_GT(r.sharpness, kDefaults.sharpness_min);
}

TEST(Quality, SharpNoiseIsAccepted) {
  const QualityResult r = evaluateQuality(makeNoise(1000, 800), kDefaults);
  EXPECT_TRUE(r.accepted);
}

TEST(Quality, StronglyBlurredImageIsRejectedAsBlurry) {
  const QualityResult r = evaluateQuality(strongBlur(makeCheckerboard(1600, 1200)), kDefaults);
  EXPECT_FALSE(r.accepted);
  EXPECT_LT(r.sharpness, kDefaults.sharpness_min);
  EXPECT_EQ(r.reasons, std::vector<std::string>{"borrosa"});
}

TEST(Quality, AlmostWhiteImageIsRejectedAsSaturated) {
  cv::Mat img = makeNoise(1000, 800);
  img.setTo(cv::Scalar(255));
  // Deja una franja con textura para que no sea además borrosa.
  makeNoise(1000, 200).copyTo(img(cv::Rect(0, 0, 1000, 200)));
  const QualityResult r = evaluateQuality(img, kDefaults);
  EXPECT_FALSE(r.accepted);
  EXPECT_GT(r.overexposed_ratio, kDefaults.overexposed_max);
  EXPECT_NE(std::find(r.reasons.begin(), r.reasons.end(), "saturada"), r.reasons.end());
}

TEST(Quality, UniformWhiteImageIsBlurryAndSaturated) {
  cv::Mat img(600, 800, CV_8UC1, cv::Scalar(255));
  const QualityResult r = evaluateQuality(img, kDefaults);
  EXPECT_FALSE(r.accepted);
  EXPECT_DOUBLE_EQ(r.overexposed_ratio, 1.0);
  EXPECT_EQ(r.reasons, (std::vector<std::string>{"borrosa", "saturada"}));
}

TEST(Quality, VerdictDoesNotDependOnResolution) {
  const cv::Mat small = makeCheckerboard(1000, 750);
  const cv::Mat large = makeCheckerboard(4000, 3000);
  const QualityResult rs = evaluateQuality(small, kDefaults);
  const QualityResult rl = evaluateQuality(large, kDefaults);
  EXPECT_EQ(rs.accepted, rl.accepted);
  EXPECT_NEAR(rs.sharpness, rl.sharpness, 0.25 * rs.sharpness);

  const QualityResult bs = evaluateQuality(strongBlur(small), kDefaults);
  const QualityResult bl = evaluateQuality(strongBlur(large), kDefaults);
  EXPECT_FALSE(bs.accepted);
  EXPECT_EQ(bs.accepted, bl.accepted);
}

// Calibración con fotos reales (sesión 20261120-204353): el fondo liso y desenfocado
// bajaba la nitidez de toda la imagen aunque el objeto central estuviera nítido.
TEST(Quality, SharpCenteredObjectOnPlainBackgroundIsAccepted) {
  cv::Mat img(750, 1000, CV_8UC1, cv::Scalar(120));
  cv::Mat center = img(cv::Rect(250, 187, 500, 375));
  cv::RNG rng(7);
  rng.fill(center, cv::RNG::UNIFORM, 114, 127);  // textura fina de bajo contraste

  const QualityResult r = evaluateQuality(img, kDefaults);
  EXPECT_TRUE(r.accepted) << "nitidez = " << r.sharpness;
  EXPECT_GT(r.sharpness, kDefaults.sharpness_min);

  // La misma textura medida en toda la imagen quedaría bajo el umbral.
  cv::Mat lap;
  cv::Laplacian(img, lap, CV_64F);
  cv::Scalar mean, stddev;
  cv::meanStdDev(lap, mean, stddev);
  EXPECT_LT(stddev[0] * stddev[0], kDefaults.sharpness_min);
}

TEST(Quality, TextureOnlyOutsideCenterIsBlurry) {
  cv::Mat img(750, 1000, CV_8UC1, cv::Scalar(120));
  makeNoise(1000, 150).copyTo(img(cv::Rect(0, 0, 1000, 150)));  // franja superior, fuera del centro
  const QualityResult r = evaluateQuality(img, kDefaults);
  EXPECT_FALSE(r.accepted);
  EXPECT_EQ(r.reasons, std::vector<std::string>{"borrosa"});
}

TEST(Quality, ThresholdsAreConfigurable) {
  QualityThresholds strict;
  strict.sharpness_min = 1e9;
  EXPECT_FALSE(evaluateQuality(makeCheckerboard(800, 600), strict).accepted);
}

TEST(Quality, EmptyImageThrows) {
  EXPECT_THROW(evaluateQuality(cv::Mat(), kDefaults), std::invalid_argument);
}
