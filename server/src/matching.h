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
// mutual_check (T0b, Sprint 4; apagado por defecto): solo se conserva a-b si además el vecino
// más cercano de b en A es a.
std::vector<FeatureMatch> matchFeatures(const ImageFeatures& a, const ImageFeatures& b, double ratio,
                                        bool mutual_check = false);

// Emparejamiento guiado por la geometría (T0b, Sprint 4): para cada punto de A solo se
// consideran los puntos de B con distancia de Sampson a F <= max_sampson_px (x_bᵀ F x_a = 0);
// entre ellos se aplica el mismo ratio test (hacen falta al menos 2 candidatos). Si varios
// puntos de A eligen el mismo de B, se queda el de menor distancia de descriptor.
std::vector<FeatureMatch> matchFeaturesGuided(const ImageFeatures& a, const ImageFeatures& b, const cv::Matx33d& F,
                                              double max_sampson_px, double ratio, bool mutual_check = false);
