// Bundle adjustment con Ceres (HU-16).
//
// Parámetros: por cámara, cuaternión [w, x, y, z] (ceres::QuaternionManifold) y
// traslación; por punto, sus 3 coordenadas; intrínsecos compartidos [f, k1, k2] con el
// punto principal fijo. Residuo: error de reproyección 2D con HuberLoss(huber_delta_px).
// Libertad de calibre: la pose de `fixed_camera` es constante y, en `scale_camera`, la
// componente de mayor magnitud de la traslación también (SubsetManifold), lo que fija
// la escala.
#pragma once

#include "sfm_types.h"

#include <string>

struct BundleOptions {
  int max_iterations = 100;
  double huber_delta_px = 1.0;
  bool refine_focal = true;
  bool refine_distortion = true;
  int fixed_camera = -1;  // índice de la foto con pose fija (-1: la primera registrada)
  int scale_camera = -1;  // índice de la foto que fija la escala (-1: la segunda registrada)
};

struct BundleReport {
  std::string stage;        // "incremental", "final", "final_2" (lo pone quien llama)
  std::string solver;       // "SPARSE_SCHUR" o "DENSE_SCHUR"
  int iterations = 0;
  double initial_cost = 0;  // costo de Ceres (con Huber)
  double final_cost = 0;
  double rms_before_px = 0; // RMS del error de reproyección (sin Huber)
  double rms_after_px = 0;
  int n_cameras = 0;
  int n_points = 0;
  int n_observations = 0;
  int retriangulated = 0;   // pistas trianguladas de nuevo después de este BA (fase incremental)
  std::string termination;  // CONVERGENCE, NO_CONVERGENCE, ...
  double time_ms = 0;
};

// Tipo de resolvedor lineal disponible en esta compilación de Ceres.
std::string bundleSolverName();

// RMS (raíz del promedio de |error|², en píxeles) sobre todas las observaciones de
// los puntos en cámaras registradas.
double reprojectionRms(const Reconstruction& rec);

BundleReport bundleAdjust(Reconstruction& rec, const BundleOptions& options);
