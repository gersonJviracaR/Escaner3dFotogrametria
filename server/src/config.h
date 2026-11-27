// Configuración del servidor: puerto, carpeta de datos y umbrales de calidad.
#pragma once

#include "matching_params.h"
#include "pipeline_params.h"
#include "reconstruction_params.h"
#include "quality.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

class ConfigError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct ServerConfig {
  int port = 8080;
  std::string data_dir = "sessions";
  std::size_t max_photo_bytes = 64u * 1024u * 1024u;
  QualityThresholds quality;
  MatchingParams matching;              // la usa scanner_match (Sprint 2), no el servidor HTTP
  ReconstructionParams reconstruction;  // la usa scanner_reconstruct (Sprint 3)
  DenseParams dense;                    // Sprint 4: scanner_dense
  MeshParams mesh;
  TextureParams texture;                // Sprint 4: scanner_texture
  ViewerParams viewer;                  // Sprint 4: scanner_view
  AccuracyParams accuracy;              // Sprint 4: scanner_accuracy
};

// Aplica sobre `base` solo los campos presentes en el JSON.
ServerConfig applyConfigJson(ServerConfig base, const nlohmann::json& j);
ServerConfig applyConfigFile(ServerConfig base, const std::filesystem::path& file);

// Interpreta --config <archivo>, --port <n> y --data-dir <ruta> (en ese orden de prioridad:
// valores de base < archivo --config < opciones --port/--data-dir).
ServerConfig applyCommandLine(ServerConfig base, const std::vector<std::string>& args);
