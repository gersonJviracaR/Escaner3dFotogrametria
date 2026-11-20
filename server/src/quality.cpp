#include "quality.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <stdexcept>

namespace {

cv::Mat toNormalizedGray(const cv::Mat& image) {
  cv::Mat gray;
  if (image.channels() == 3)
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
  else if (image.channels() == 4)
    cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
  else
    gray = image;

  const int longSide = std::max(gray.cols, gray.rows);
  if (longSide == kQualityLongSide) return gray;
  const double scale = static_cast<double>(kQualityLongSide) / longSide;
  cv::Mat resized;
  cv::resize(gray, resized, cv::Size(), scale, scale,
             scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);
  return resized;
}

}  // namespace

QualityResult evaluateQuality(const cv::Mat& image, const QualityThresholds& thresholds) {
  if (image.empty()) throw std::invalid_argument("imagen vacía");

  const cv::Mat gray = toNormalizedGray(image);

  cv::Mat laplacian;
  cv::Laplacian(gray, laplacian, CV_64F);
  cv::Scalar mean, stddev;
  cv::meanStdDev(laplacian, mean, stddev);

  QualityResult result;
  result.sharpness = stddev[0] * stddev[0];
  result.overexposed_ratio =
      static_cast<double>(cv::countNonZero(gray >= kOverexposedLevel)) / gray.total();

  if (result.sharpness < thresholds.sharpness_min) result.reasons.push_back("borrosa");
  if (result.overexposed_ratio > thresholds.overexposed_max) result.reasons.push_back("saturada");
  result.accepted = result.reasons.empty();
  return result;
}
