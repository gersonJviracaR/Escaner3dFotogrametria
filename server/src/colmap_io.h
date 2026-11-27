// Modelo COLMAP en formato de texto (cameras.txt, images.txt, points3D.txt): exportación de
// la reconstrucción propia (HU-19) y lectura de un modelo de COLMAP (HU-24).
//
// Convenciones: COLMAP usa la misma pose mundo -> cámara y cuaterniones [w, x, y, z], así que
// q y t se copian sin transformar. COLMAP pone el centro del primer píxel en (0,5, 0,5) y
// nosotros en (0, 0): al exportar se suma 0,5 a las observaciones y al punto principal, y al
// leer se resta.
#pragma once

#include "camera_model.h"
#include "sfm_types.h"

#include <opencv2/core.hpp>

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class ColmapFormatError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct ColmapImage {
  int image_id = 0;
  int camera_id = 1;
  std::string name;  // "img_001.jpg"
  Pose pose;
  std::vector<cv::Point2d> points2d;       // en la convención propia (centro del píxel (0,0) en (0,0))
  std::vector<long long> point3d_ids;      // -1 si la observación no tiene punto 3D
};

struct ColmapPoint {
  long long id = 0;
  cv::Vec3d xyz{0, 0, 0};
  cv::Vec3b rgb{0, 0, 0};
  double error = 0;
  std::vector<std::pair<int, int>> track;  // (image_id, índice del punto 2D en esa imagen)
};

struct ColmapModel {
  std::string camera_model = "RADIAL";  // modelo de la (única) cámara
  CameraModel camera;                    // f, cx, cy, k1, k2 en la convención propia
  std::vector<ColmapImage> images;       // ordenadas por image_id
  std::vector<ColmapPoint> points;
};

// Solo cámaras registradas; image_id = índice de la foto; points3D con sus pistas.
// `names` da el archivo de cada foto (por índice).
ColmapModel toColmapModel(const Reconstruction& rec, const std::map<int, std::string>& names);

void writeColmapText(const std::filesystem::path& dir, const ColmapModel& model);
// Lee cameras.txt, images.txt y points3D.txt. Admite SIMPLE_PINHOLE, PINHOLE (fx = fy),
// SIMPLE_RADIAL y RADIAL con una sola cámara. Lanza ColmapFormatError si algo no cuadra.
ColmapModel readColmapText(const std::filesystem::path& dir);

// Cámaras del modelo como Reconstruction (para texturizar con poses de COLMAP).
// `index_of` da el índice de la foto a partir del nombre; las imágenes sin índice se ignoran.
Reconstruction toReconstruction(const ColmapModel& model, const std::map<std::string, int>& index_of);
