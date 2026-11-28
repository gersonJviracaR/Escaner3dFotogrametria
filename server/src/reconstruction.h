// Orquestación de la reconstrucción dispersa incremental (Epic 3).
#pragma once

#include "bundle_adjustment.h"
#include "initialization.h"
#include "reconstruction_params.h"
#include "reconstruction_report.h"
#include "visibility.h"
#include "sfm_types.h"

#include <functional>
#include <string>
#include <vector>

struct SfmInput {
  CameraModel camera;                // intrínsecos iniciales (sin distorsión)
  std::vector<int> images;           // fotos candidatas (índices del manifiesto)
  std::vector<PairCandidate> pairs;  // pares aceptados con sus inliers y E
  std::vector<Track> tracks;
};

struct FocalTrial {
  double factor = 1;
  int points = 0;               // puntos del par inicial dentro de los controles de HU-14
  double median_error_px = 0;   // error de reproyección mediano de esos puntos
};

struct SfmResult {
  Reconstruction rec;
  InitialPair init;
  int init_points = 0;                // puntos triangulados del par inicial (con la focal elegida)
  double init_focal_factor = 1.0;     // factor aplicado a la focal inicial (1 si no hizo falta la rejilla)
  std::vector<FocalTrial> focal_trials;  // rejilla probada (vacía si la K original triangula)
  std::vector<BundleReport> ba_runs;  // en orden de ejecución
  FilterStats filter_first;           // filtrado tras el BA final (HU-17)
  FilterStats filter_final;           // filtrado tras el segundo BA
  CameraErrors errors_before;         // HU-18: antes del BA final (tras la fase incremental)
  CameraErrors errors_after;          // HU-18: después del segundo BA y del filtrado final
};

// Flujo de la sección 3.2: par inicial, triangulación, registro incremental con PnP.
SfmResult runIncrementalSfm(const SfmInput& input, const ReconstructionParams& params,
                            const std::function<void(const std::string&)>& log = {});
