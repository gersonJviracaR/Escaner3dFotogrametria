// Extracción de características SIFT por foto (HU-07) con normalización de contraste (HU-08).
#pragma once

#include "matching_params.h"

#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

constexpr int kSiftDescriptorSize = 128;

class FeatureFileError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Características de una foto. Las coordenadas (pt) y el tamaño (size) de cada punto
// están en píxeles de la imagen ORIGINAL, aunque se hayan extraído sobre la imagen
// reducida: así la matriz K de la foto original sirve sin reescalar.
struct ImageFeatures {
  int original_width = 0;
  int original_height = 0;
  int work_width = 0;   // tamaño de la imagen sobre la que corrió SIFT
  int work_height = 0;
  double scale = 1.0;   // work = original * scale (scale <= 1)
  std::vector<cv::KeyPoint> keypoints;
  cv::Mat descriptors;  // keypoints.size() x 128, CV_32F
};

// Gris, reducido a work_long_side en el lado mayor (INTER_AREA; nunca se amplía) y,
// si params.clahe, con CLAHE. Devuelve en `scale` el factor aplicado.
cv::Mat prepareWorkImage(const cv::Mat& image, const MatchingParams& params, double& scale);

// Lanza std::invalid_argument si la imagen está vacía.
ImageFeatures extractFeatures(const cv::Mat& image, const MatchingParams& params);

// "img_001.feat"
std::string featureFileName(int index);

// Formato binario documentado en docs/formatos.md.
void writeFeatures(const std::filesystem::path& file, const ImageFeatures& f);
ImageFeatures readFeatures(const std::filesystem::path& file);
