// Error de reproyección por cámara (HU-18) y reporte de la reconstrucción
// (reconstruction_report.json, formato en docs/formatos.md).
#pragma once

#include "bundle_adjustment.h"
#include "initialization.h"
#include "reconstruction_params.h"
#include "sfm_types.h"
#include "visibility.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

struct ErrorStats {
  int n = 0;          // observaciones
  double mean = 0;    // media de |error| (px)
  double rms = 0;     // raíz del promedio de |error|² (px)
  double max = 0;     // máximo |error| (px)
};

struct CameraErrors {
  std::map<int, ErrorStats> per_camera;  // solo cámaras registradas
  ErrorStats global;
};

// Errores de reproyección de las observaciones de los puntos, agrupados por cámara.
CameraErrors computeCameraErrors(const Reconstruction& rec);

struct CameraSummary {
  int index = 0;
  bool registered = false;
  std::string reason;
  ErrorStats before;  // antes del BA final (tras la fase incremental)
  ErrorStats after;   // después del segundo BA y del filtrado final
};

struct Stats {
  double min = 0, median = 0, mean = 0, max = 0;
};
Stats describe(std::vector<double> values);

struct ReconstructionReport {
  std::string session_id;
  std::string generated_at;
  ReconstructionParams params;
  std::string solver;
  CameraModel initial_intrinsics;
  CameraModel final_intrinsics;
  std::vector<InitialPairChoice> init_evaluated;
  int init_i = -1;
  int init_j = -1;
  int init_points = 0;                  // puntos triangulados del par inicial
  double init_focal_factor = 1.0;       // factor de focal usado en el par inicial
  nlohmann::json focal_search = nlohmann::json::array();  // rejilla probada (vacía si no hizo falta)
  std::vector<std::pair<int, std::string>> excluded_photos;  // fuera del pipeline (p. ej. calidad)
  std::vector<CameraSummary> cameras;
  ErrorStats global_before;
  ErrorStats global_after;
  int tracks = 0;
  int tracks_discarded_inconsistent = 0;
  int points_before_filter = 0;  // antes del primer filtrado
  int points_after_filter = 0;   // al final
  FilterStats filter_first;
  FilterStats filter_final;
  Stats track_length;            // observaciones por punto final
  std::vector<BundleReport> ba_runs;
  nlohmann::json imu_consistency = nullptr;  // comprobación informativa (sección 4.2)
  nlohmann::json ring_geometry = nullptr;
  std::map<std::string, double> timing_ms;
  std::vector<std::string> warnings;
};

nlohmann::json toJson(const ErrorStats& e);
nlohmann::json toJson(const ReconstructionReport& r);
