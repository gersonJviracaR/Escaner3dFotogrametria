// Imágenes JPEG sintéticas para las pruebas.
#pragma once

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

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
