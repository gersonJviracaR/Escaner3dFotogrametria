// Pruebas de HU-07 (extracción SIFT) y HU-08 (normalización de contraste).
#include "features.h"
#include "matching.h"
#include "test_images.h"

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace fs = std::filesystem;

namespace {

// Manchas gaussianas brillantes sobre fondo oscuro en posiciones conocidas: SIFT
// (diferencia de gaussianas) debe detectar un punto en el centro de cada una.
struct KnownPoints {
  cv::Mat image;
  std::vector<cv::Point2f> centers;
};

KnownPoints gaussianSpots(int width, int height, double sigma) {
  KnownPoints k;
  cv::Mat img(height, width, CV_32F, cv::Scalar(30));
  const double step = 8 * sigma;
  for (double y = step; y < height - step / 2; y += step)
    for (double x = step; x < width - step / 2; x += step) {
      // pequeña desviación pseudoaleatoria para no tener una rejilla perfecta
      const double cx = x + 0.37 * sigma * std::sin(x * 0.11 + y * 0.07);
      const double cy = y + 0.37 * sigma * std::cos(x * 0.05 - y * 0.13);
      k.centers.emplace_back(static_cast<float>(cx), static_cast<float>(cy));
      const int r = static_cast<int>(4 * sigma);
      for (int yy = std::max(0, int(cy) - r); yy <= std::min(height - 1, int(cy) + r); ++yy)
        for (int xx = std::max(0, int(cx) - r); xx <= std::min(width - 1, int(cx) + r); ++xx) {
          const double d2 = (xx - cx) * (xx - cx) + (yy - cy) * (yy - cy);
          img.at<float>(yy, xx) += static_cast<float>(180 * std::exp(-d2 / (2 * sigma * sigma)));
        }
    }
  img.convertTo(k.image, CV_8U);
  return k;
}

double nearestDistance(const std::vector<cv::KeyPoint>& kps, const cv::Point2f& p) {
  double best = std::numeric_limits<double>::max();
  for (const cv::KeyPoint& kp : kps)
    best = std::min(best, std::hypot(double(kp.pt.x) - p.x, double(kp.pt.y) - p.y));
  return best;
}

double fractionDetected(const ImageFeatures& f, const std::vector<cv::Point2f>& centers, double tol) {
  int found = 0;
  for (const cv::Point2f& c : centers)
    if (nearestDistance(f.keypoints, c) <= tol) ++found;
  return static_cast<double>(found) / static_cast<double>(centers.size());
}

MatchingParams paramsWithWorkSide(int side) {
  MatchingParams p;
  p.work_long_side = side;
  return p;
}

}  // namespace

// ---------- HU-07 ----------

TEST(Features, DetectsKnownPointsWith128FloatDescriptors) {
  const KnownPoints k = gaussianSpots(1200, 900, 6.0);
  const ImageFeatures f = extractFeatures(k.image, paramsWithWorkSide(2000));
  ASSERT_FALSE(f.keypoints.empty());
  EXPECT_EQ(f.descriptors.rows, static_cast<int>(f.keypoints.size()));
  EXPECT_EQ(f.descriptors.cols, kSiftDescriptorSize);
  EXPECT_EQ(f.descriptors.type(), CV_32F);
  EXPECT_GE(fractionDetected(f, k.centers, 1.5), 0.95);
  EXPECT_DOUBLE_EQ(f.scale, 1.0);  // nunca se amplía
  EXPECT_EQ(f.work_width, 1200);
}

TEST(Features, CoordinatesAreInOriginalPixelsWhenDownscaled) {
  const KnownPoints k = gaussianSpots(1600, 1200, 8.0);
  const ImageFeatures f = extractFeatures(k.image, paramsWithWorkSide(800));
  EXPECT_DOUBLE_EQ(f.scale, 0.5);
  EXPECT_EQ(f.original_width, 1600);
  EXPECT_EQ(f.original_height, 1200);
  EXPECT_EQ(f.work_width, 800);
  EXPECT_EQ(f.work_height, 600);
  // Si las coordenadas quedaran en la imagen reducida, caerían a la mitad de distancia del origen.
  EXPECT_GE(fractionDetected(f, k.centers, 2.5), 0.95);
  float max_x = 0;
  for (const cv::KeyPoint& kp : f.keypoints) max_x = std::max(max_x, kp.pt.x);
  EXPECT_GT(max_x, 1000.0f);
}

TEST(Features, ColorImageIsAccepted) {
  cv::Mat color;
  cv::cvtColor(testimg::blobTexture(640, 480, 3), color, cv::COLOR_GRAY2BGR);
  EXPECT_GT(extractFeatures(color, MatchingParams{}).keypoints.size(), 100u);
}

TEST(Features, RespectsMaxFeatures) {
  MatchingParams p;
  p.max_features = 150;
  const ImageFeatures f = extractFeatures(testimg::blobTexture(1000, 750, 5), p);
  EXPECT_LE(f.keypoints.size(), 150u);
  EXPECT_GE(f.keypoints.size(), 140u);
  EXPECT_EQ(f.descriptors.rows, static_cast<int>(f.keypoints.size()));
}

TEST(Features, EmptyImageThrows) {
  EXPECT_THROW(extractFeatures(cv::Mat(), MatchingParams{}), std::invalid_argument);
}

TEST(Features, FileNameIsZeroPadded) { EXPECT_EQ(featureFileName(7), "img_007.feat"); }

TEST(Features, FileRoundTrip) {
  const ImageFeatures f = extractFeatures(testimg::blobTexture(900, 600, 11), paramsWithWorkSide(600));
  ASSERT_GT(f.keypoints.size(), 10u);
  const fs::path file =
      fs::temp_directory_path() /
      ("escaner3d_feat_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".feat");
  writeFeatures(file, f);
  EXPECT_FALSE(fs::exists(fs::path(file.string() + ".tmp")));
  const ImageFeatures g = readFeatures(file);
  fs::remove(file);

  EXPECT_EQ(g.original_width, f.original_width);
  EXPECT_EQ(g.original_height, f.original_height);
  EXPECT_EQ(g.work_width, f.work_width);
  EXPECT_EQ(g.work_height, f.work_height);
  EXPECT_DOUBLE_EQ(g.scale, f.scale);
  ASSERT_EQ(g.keypoints.size(), f.keypoints.size());
  for (std::size_t i = 0; i < f.keypoints.size(); ++i) {
    EXPECT_EQ(g.keypoints[i].pt, f.keypoints[i].pt);
    EXPECT_EQ(g.keypoints[i].size, f.keypoints[i].size);
    EXPECT_EQ(g.keypoints[i].angle, f.keypoints[i].angle);
    EXPECT_EQ(g.keypoints[i].response, f.keypoints[i].response);
    EXPECT_EQ(g.keypoints[i].octave, f.keypoints[i].octave);
  }
  ASSERT_EQ(g.descriptors.type(), CV_32F);
  EXPECT_EQ(cv::norm(g.descriptors, f.descriptors, cv::NORM_INF), 0.0);
}

// ---------- HU-08 ----------

namespace {

// Copia con brillo y contraste alterados: contraste al 30 % y luz no uniforme
// (gradiente horizontal de +100 a +180 niveles).
cv::Mat alterBrightnessContrast(const cv::Mat& gray) {
  cv::Mat out(gray.size(), CV_8UC1);
  for (int y = 0; y < gray.rows; ++y)
    for (int x = 0; x < gray.cols; ++x)
      out.at<uchar>(y, x) =
          cv::saturate_cast<uchar>(0.3 * gray.at<uchar>(y, x) + 100.0 + 80.0 * x / gray.cols);
  return out;
}

struct TwoViews {
  cv::Mat a, b;
  cv::Matx23d transform;  // p_b = transform * [p_a; 1]
};

TwoViews twoViews() {
  TwoViews v;
  v.a = testimg::blobTexture(800, 600, 31);
  const cv::Mat m = cv::getRotationMatrix2D(cv::Point2f(400, 300), 12.0, 0.9);
  v.transform = cv::Matx23d(m);
  cv::warpAffine(v.a, v.b, m, v.a.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT);
  return v;
}

// Correspondencias con ratio test (0,75) que respetan la transformación conocida.
int correctMatches(const ImageFeatures& a, const ImageFeatures& b, const cv::Matx23d& t) {
  int correct = 0;
  for (const FeatureMatch& m : matchFeatures(a, b, 0.75)) {
    const cv::Vec2d expected = t * cv::Vec3d(m.point_a.x, m.point_a.y, 1.0);
    if (std::hypot(expected[0] - m.point_b.x, expected[1] - m.point_b.y) <= 3.0) ++correct;
  }
  return correct;
}

}  // namespace

TEST(ContrastNormalization, KeepsAtLeast80PercentOfMatchesUnderBrightnessContrastChange) {
  const TwoViews v = twoViews();
  MatchingParams p;
  p.clahe = true;
  const ImageFeatures fb = extractFeatures(v.b, p);
  const int baseline = correctMatches(extractFeatures(v.a, p), fb, v.transform);
  const int altered = correctMatches(extractFeatures(alterBrightnessContrast(v.a), p), fb, v.transform);
  ASSERT_GT(baseline, 200);
  EXPECT_GE(altered, 0.8 * baseline) << "alterada: " << altered << ", sin alterar: " << baseline;
  std::cout << "[ info ] HU-08 con CLAHE: " << altered << " de " << baseline << " correspondencias ("
            << 100.0 * altered / baseline << " %)\n";
}

TEST(ContrastNormalization, PipelineStillWorksWithoutClahe) {
  const TwoViews v = twoViews();
  MatchingParams p;
  p.clahe = false;
  EXPECT_GT(correctMatches(extractFeatures(v.a, p), extractFeatures(v.b, p), v.transform), 200);
}

TEST(ContrastNormalization, ClaheRestoresLocalContrast) {
  const cv::Mat altered = alterBrightnessContrast(testimg::blobTexture(800, 600, 31));
  MatchingParams p;
  double scale = 0;
  p.clahe = false;
  const cv::Mat plain = prepareWorkImage(altered, p, scale);
  EXPECT_EQ(cv::norm(plain, altered, cv::NORM_INF), 0.0);  // sin CLAHE no se toca
  p.clahe = true;
  const cv::Mat equalized = prepareWorkImage(altered, p, scale);
  // Contraste local = desviación de la componente de alta frecuencia (sin el gradiente de luz).
  auto localContrast = [](const cv::Mat& img) {
    cv::Mat f, low;
    img.convertTo(f, CV_32F);
    cv::GaussianBlur(f, low, cv::Size(0, 0), 8.0);
    cv::Scalar mean, stddev;
    cv::meanStdDev(f - low, mean, stddev);
    return stddev[0];
  };
  EXPECT_GT(localContrast(equalized), 1.5 * localContrast(altered));
}

TEST(Features, ReadingInvalidFileThrows) {
  const fs::path file = fs::temp_directory_path() / "escaner3d_feat_invalido.feat";
  {
    std::ofstream out(file, std::ios::binary);
    out << "esto no es un archivo de características";
  }
  EXPECT_THROW(readFeatures(file), FeatureFileError);
  fs::remove(file);
  EXPECT_THROW(readFeatures(file), FeatureFileError);  // no existe
}
