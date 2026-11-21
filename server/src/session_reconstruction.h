// Reconstrucción dispersa de una sesión ya emparejada por scanner_match (Epic 3).
#pragma once

#include "reconstruction.h"
#include "reconstruction_report.h"

#include <filesystem>
#include <functional>
#include <string>

struct SessionReconstructionOptions {
  bool include_rejected = false;  // incluir fotos con quality.accepted = false
  bool debug = false;             // imágenes de la nube vista desde arriba y de costado
  std::function<void(const std::string&)> log;
};

struct SessionReconstructionResult {
  SfmResult sfm;
  ReconstructionReport report;
};

// Lee <sesión>/manifest.json, matching_report.json y matches/*.csv (pares aceptados),
// reconstruye y escribe en <sesión>/reconstruction/:
//   reconstruction.json, reconstruction_report.json, sparse_cloud.ply
//   (y con debug: debug_top.jpg, debug_side.jpg).
// Lanza std::runtime_error si falta la sesión, el manifiesto o el matching_report.json.
SessionReconstructionResult runSessionReconstruction(const std::filesystem::path& session_dir,
                                                     ReconstructionParams params,
                                                     const SessionReconstructionOptions& options = {});
