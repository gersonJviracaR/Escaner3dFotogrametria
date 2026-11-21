// Orquestación de la reconstrucción dispersa incremental (Epic 3).
#pragma once

#include "bundle_adjustment.h"
#include "initialization.h"
#include "reconstruction_params.h"
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

struct SfmResult {
  Reconstruction rec;
  InitialPair init;
  std::vector<BundleReport> ba_runs;  // en orden de ejecución
  FilterStats filter_first;           // filtrado tras el BA final (HU-17)
  FilterStats filter_final;           // filtrado tras el segundo BA
};

// Flujo de la sección 3.2: par inicial, triangulación, registro incremental con PnP.
SfmResult runIncrementalSfm(const SfmInput& input, const ReconstructionParams& params,
                            const std::function<void(const std::string&)>& log = {});
