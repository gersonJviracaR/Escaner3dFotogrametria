// Almacenamiento de sesiones de captura en disco (HU-05).
#pragma once

#include "manifest.h"
#include "quality.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

class SessionNotFoundError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// El contenido recibido no es una imagen decodificable (HTTP 400).
class InvalidImageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Deja solo [A-Za-z0-9_-], reemplaza el resto por '_' y recorta a 40 caracteres.
std::string sanitizeSessionName(const std::string& name);
// Un id válido solo contiene [A-Za-z0-9_-] (evita rutas como "../").
bool isValidSessionId(const std::string& id);
// ISO 8601 en hora local con desfase, p. ej. "2026-11-10T15:30:00-04:00".
std::string formatLocalTimestamp(std::chrono::system_clock::time_point t);

// Hilo seguro: el servidor HTTP atiende peticiones en paralelo.
class SessionStore {
 public:
  SessionStore(std::filesystem::path root, QualityThresholds thresholds);

  std::string createSession(const std::string& name, const nlohmann::json& device,
                            std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
  bool exists(const std::string& id) const;

  // Guarda la foto (reemplaza si el índice ya existe), la valida y devuelve el registro.
  PhotoRecord addPhoto(const std::string& id, const PhotoMetadata& metadata, const std::string& jpegBytes);
  // Escribe manifest.json y devuelve el resumen. Puede llamarse de nuevo tras repetir tomas.
  Summary complete(const std::string& id);
  Manifest manifest(const std::string& id) const;

  const std::filesystem::path& root() const { return root_; }

 private:
  Manifest& findOrThrow(const std::string& id);
  const Manifest& findOrThrow(const std::string& id) const;

  std::filesystem::path root_;
  QualityThresholds thresholds_;
  mutable std::mutex mutex_;
  std::map<std::string, Manifest> sessions_;
};
