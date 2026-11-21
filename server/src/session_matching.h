// Procesa una sesión ya almacenada: características, pares vecinos, F/E y reporte (Epic 2).
#pragma once

#include "matching_report.h"

#include <filesystem>
#include <functional>
#include <string>

struct SessionMatchingOptions {
  bool debug = false;  // guarda en debug/ imágenes con correspondencias (drawMatches)
  std::function<void(const std::string&)> log;  // progreso; puede ser nulo
};

// Lee <sesión>/manifest.json, usa las fotos aceptadas por calidad que tengan orientación,
// y escribe en la sesión:
//   features/img_###.feat        características de cada foto usada
//   matches/pair_###_###.csv     inliers de cada par ACEPTADO (la carpeta se vacía antes)
//   matching_report.json         parámetros, K, tiempos y resultado de cada par
// Lanza std::runtime_error si la sesión o su manifiesto no existen.
MatchingReport runSessionMatching(const std::filesystem::path& session_dir, const MatchingParams& params,
                                  const SessionMatchingOptions& options = {});
