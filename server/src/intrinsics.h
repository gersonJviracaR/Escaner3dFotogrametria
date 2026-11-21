// Matriz intrínseca K inicial de cada foto a partir del manifiesto (sección 3.3 del Sprint 2).
//
// Es una APROXIMACIÓN: supone que la imagen no está recortada respecto al sensor,
// punto principal en el centro, píxeles cuadrados, sin sesgo ni distorsión. El bundle
// adjustment del Sprint 3 la refinará.
#pragma once

#include "manifest.h"

#include <opencv2/core.hpp>

constexpr double kFullFrameDiagonalMm = 43.27;  // diagonal de un fotograma de 35 mm (36 x 24 mm)

struct CameraIntrinsics {
  double focal_px = 0;
  double cx = 0;
  double cy = 0;
  int width = 0;   // resolución original de la foto
  int height = 0;
  bool used_fallback = false;  // true si no había focal_35mm y se usó fallback_focal_factor

  cv::Matx33d matrix() const;
};

// f = focal_35mm * diagonal_px / 43,27, o bien f = fallback_focal_factor * max(ancho, alto)
// si focal_35mm falta o no es positiva. Lanza std::invalid_argument sin ancho/alto.
CameraIntrinsics intrinsicsFromMetadata(const PhotoMetadata& photo, double fallback_focal_factor);
