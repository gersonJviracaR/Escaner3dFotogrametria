// Metadatos de cada foto y manifiesto de la sesión (HU-02, HU-05).
#pragma once

#include "quality.h"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

constexpr int kManifestSchemaVersion = 1;
constexpr int kMaxPhotoIndex = 999;

// Metadatos mal formados (se traduce a HTTP 400).
class MetadataError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Datos crudos que envía la app por cada foto.
struct PhotoMetadata {
  int index = 0;
  std::string captured_at;
  int width = 0;
  int height = 0;
  std::optional<double> focal_mm;
  std::optional<double> focal_35mm;
  std::optional<std::array<double, 4>> orientation_q;  // [w, x, y, z]
};

struct PhotoRecord {
  PhotoMetadata metadata;
  std::string file;
  QualityResult quality;
};

struct Summary {
  int total = 0;
  int accepted = 0;
  std::vector<int> rejected;  // índices ordenados
};

struct Manifest {
  int schema_version = kManifestSchemaVersion;
  std::string session_id;
  std::string created_at;
  nlohmann::json device = nlohmann::json::object();
  std::vector<PhotoRecord> photos;
};

// "img_001.jpg"
std::string photoFileName(int index);

// Lanza MetadataError si el JSON no es válido.
PhotoMetadata parsePhotoMetadata(const nlohmann::json& j);
nlohmann::json toJson(const PhotoMetadata& m);
nlohmann::json toJson(const QualityResult& q);
// Resultado de validación de una foto tal como lo devuelve la API.
nlohmann::json validationJson(const PhotoRecord& photo);

Summary summarize(const std::vector<PhotoRecord>& photos);
nlohmann::json toJson(const Summary& s);

nlohmann::json toJson(const Manifest& m);
Manifest manifestFromJson(const nlohmann::json& j);

// Escribe en un archivo temporal y lo renombra, para no dejar manifiestos a medias.
void writeManifestAtomic(const std::filesystem::path& file, const Manifest& m);
Manifest readManifest(const std::filesystem::path& file);
