// Emparejamiento de descriptores SIFT entre dos fotos con ratio test (HU-09).
#pragma once

#include "features.h"

#include <opencv2/core.hpp>

#include <vector>

// Correspondencia candidata entre el punto index_a de la foto A y el index_b de la foto B.
// Las coordenadas están en píxeles de la imagen original de cada foto.
struct FeatureMatch {
  int index_a = -1;
  int index_b = -1;
  cv::Point2f point_a;
  cv::Point2f point_b;
  float distance = 0;  // distancia euclidiana entre descriptores
};

// kNN (k = 2) por fuerza bruta con norma L2; se acepta el vecino más cercano si
// distancia1 < ratio * distancia2 (Lowe). Sin segundo vecino no hay correspondencia.
std::vector<FeatureMatch> matchFeatures(const ImageFeatures& a, const ImageFeatures& b, double ratio);
