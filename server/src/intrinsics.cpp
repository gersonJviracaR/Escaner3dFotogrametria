#include "intrinsics.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

cv::Matx33d CameraIntrinsics::matrix() const {
  return {focal_px, 0, cx,  //
          0, focal_px, cy,  //
          0, 0, 1};
}

CameraIntrinsics intrinsicsFromMetadata(const PhotoMetadata& photo, double fallback_focal_factor) {
  if (photo.width <= 0 || photo.height <= 0)
    throw std::invalid_argument("la foto " + std::to_string(photo.index) + " no tiene ancho/alto en el manifiesto");
  CameraIntrinsics k;
  k.width = photo.width;
  k.height = photo.height;
  k.cx = photo.width / 2.0;
  k.cy = photo.height / 2.0;
  if (photo.focal_35mm && *photo.focal_35mm > 0) {
    // Equivalente de 35 mm: la diagonal de la imagen corresponde a 43,27 mm.
    const double diagonal_px = std::hypot(static_cast<double>(photo.width), static_cast<double>(photo.height));
    k.focal_px = *photo.focal_35mm * diagonal_px / kFullFrameDiagonalMm;
  } else {
    k.focal_px = fallback_focal_factor * std::max(photo.width, photo.height);
    k.used_fallback = true;
  }
  return k;
}
