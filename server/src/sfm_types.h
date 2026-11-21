// Estructuras de la reconstrucción dispersa (Epic 3), compartidas por registro,
// bundle adjustment, visibilidad y orquestación.
#pragma once

#include "camera_model.h"
#include "tracks.h"

#include <opencv2/core.hpp>

#include <map>
#include <string>
#include <vector>

struct Camera {
  int index = 0;            // índice de la foto en el manifiesto
  bool registered = false;
  std::string reason;       // motivo si no quedó registrada
  Pose pose;                // mundo -> cámara
};

struct Point3D {
  cv::Vec3d xyz{0, 0, 0};
  int track = -1;                         // pista de origen
  std::vector<Observation> observations;  // solo en cámaras registradas
  cv::Vec3b rgb{0, 0, 0};                 // color promedio de las observaciones (R, G, B)
};

struct Reconstruction {
  CameraModel camera;                // intrínsecos compartidos (f, k1, k2, punto principal)
  std::map<int, Camera> cameras;     // por índice del manifiesto
  std::vector<Track> tracks;
  std::vector<Point3D> points;
  std::vector<int> point_of_track;   // índice en `points` o -1
  std::vector<int> registration_order;

  int registeredCount() const;
  // Recalcula point_of_track después de borrar puntos.
  void reindexTracks();
};
