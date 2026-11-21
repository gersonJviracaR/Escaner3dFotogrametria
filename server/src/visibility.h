// Visibilidad (frustum) y filtrado de la nube (HU-17).
#pragma once

#include "sfm_types.h"

// Un punto es visible desde una cámara si está delante (z > 0) y se proyecta dentro de
// la imagen. Los centros de los píxeles tienen coordenadas enteras, así que la imagen
// ocupa [-0,5, ancho - 0,5] x [-0,5, alto - 0,5]; el borde cuenta como dentro.
bool isVisible(const CameraModel& camera, const Pose& pose, const cv::Vec3d& X);

struct FilterStats {
  int points_before = 0;
  int points_after = 0;
  int observations_not_visible = 0;   // detrás de la cámara o fuera de la imagen
  int observations_high_error = 0;    // error de reproyección > max_error_px
  int points_few_views = 0;           // eliminados por quedar con < min_views observaciones
};

// Por cada punto se conservan solo las observaciones en cámaras registradas que lo ven
// (frustum) y con error <= max_error_px; el punto se conserva si le quedan al menos
// min_views. Actualiza rec.point_of_track.
FilterStats filterPoints(Reconstruction& rec, int min_views, double max_error_px);
