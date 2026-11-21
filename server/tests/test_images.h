// Imágenes JPEG sintéticas para las pruebas.
#pragma once

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace testimg {

inline cv::Mat checkerboard(int width, int height) {
  cv::Mat img(height, width, CV_8UC3);
  const int square = std::max(1, width / 16);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const uchar v = ((x / square + y / square) % 2) ? 200 : 40;
      img.at<cv::Vec3b>(y, x) = cv::Vec3b(v, v, v);
    }
  return img;
}

// Textura aleatoria de manchas (ruido de baja resolución ampliado y suavizado), en gris.
inline cv::Mat blobTexture(int width, int height, int seed, int cell = 8) {
  cv::Mat small(std::max(2, height / cell), std::max(2, width / cell), CV_8UC1);
  cv::RNG rng(static_cast<uint64_t>(seed));
  rng.fill(small, cv::RNG::UNIFORM, 0, 256);
  cv::Mat img;
  cv::resize(small, img, cv::Size(width, height), 0, 0, cv::INTER_CUBIC);
  cv::GaussianBlur(img, img, cv::Size(0, 0), 1.2);
  return img;
}

inline std::string encodeJpeg(const cv::Mat& img) {
  std::vector<uchar> buffer;
  cv::imencode(".jpg", img, buffer, {cv::IMWRITE_JPEG_QUALITY, 90});
  return std::string(buffer.begin(), buffer.end());
}

inline std::string sharpJpeg(int width, int height) { return encodeJpeg(checkerboard(width, height)); }

inline std::string blurryJpeg(int width, int height) {
  cv::Mat blurred;
  cv::GaussianBlur(checkerboard(width, height), blurred, cv::Size(0, 0), width / 60.0);
  return encodeJpeg(blurred);
}

}  // namespace testimg
