#include "session_store.h"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxSessionNameLength = 40;

bool isSessionIdChar(unsigned char c) {
  return (c < 128 && std::isalnum(c)) || c == '-' || c == '_';
}

std::tm toLocalTm(std::chrono::system_clock::time_point t) {
  const std::time_t tt = std::chrono::system_clock::to_time_t(t);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif
  return tm;
}

// Desfase respecto a UTC en minutos para la hora local dada.
long utcOffsetMinutes(std::chrono::system_clock::time_point t) {
  const std::time_t tt = std::chrono::system_clock::to_time_t(t);
  std::tm local = toLocalTm(t);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &tt);
#else
  gmtime_r(&tt, &utc);
#endif
  utc.tm_isdst = local.tm_isdst;
  return static_cast<long>(std::difftime(std::mktime(&local), std::mktime(&utc)) / 60);
}

void writeFileAtomic(const fs::path& file, const std::string& bytes) {
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("no se pudo escribir " + tmp.string());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("error al escribir " + tmp.string());
  }
  fs::rename(tmp, file);
}

}  // namespace

std::string sanitizeSessionName(const std::string& name) {
  std::string out;
  for (unsigned char c : name) {
    if (out.size() >= kMaxSessionNameLength) break;
    out += isSessionIdChar(c) ? static_cast<char>(c) : '_';
  }
  return out;
}

bool isValidSessionId(const std::string& id) {
  if (id.empty() || id.size() > 80) return false;
  return std::all_of(id.begin(), id.end(), [](unsigned char c) { return isSessionIdChar(c); });
}

std::string formatLocalTimestamp(std::chrono::system_clock::time_point t) {
  const std::tm tm = toLocalTm(t);
  char base[32];
  std::strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm);
  const long offset = utcOffsetMinutes(t);
  const long absOffset = offset < 0 ? -offset : offset;
  char zone[8];
  std::snprintf(zone, sizeof(zone), "%c%02ld:%02ld", offset < 0 ? '-' : '+', absOffset / 60,
                absOffset % 60);
  return std::string(base) + zone;
}

SessionStore::SessionStore(fs::path root, QualityThresholds thresholds)
    : root_(std::move(root)), thresholds_(thresholds) {
  fs::create_directories(root_);
}

std::string SessionStore::createSession(const std::string& name, const nlohmann::json& device,
                                        std::chrono::system_clock::time_point now) {
  const std::tm tm = toLocalTm(now);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
  std::string base = stamp;
  const std::string clean = sanitizeSessionName(name);
  if (!clean.empty()) base += "-" + clean;

  std::lock_guard<std::mutex> lock(mutex_);
  std::string id = base;
  for (int n = 2; sessions_.count(id) || fs::exists(root_ / id); ++n) id = base + "-" + std::to_string(n);

  fs::create_directories(root_ / id);
  Manifest m;
  m.session_id = id;
  m.created_at = formatLocalTimestamp(now);
  m.device = device.is_object() ? device : nlohmann::json::object();
  sessions_.emplace(id, std::move(m));
  return id;
}

bool SessionStore::exists(const std::string& id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.count(id) > 0;
}

PhotoRecord SessionStore::addPhoto(const std::string& id, const PhotoMetadata& metadata,
                                   const std::string& jpegBytes) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    findOrThrow(id);
  }

  // Decodificar y medir fuera del candado: es la parte costosa.
  const std::vector<uchar> buffer(jpegBytes.begin(), jpegBytes.end());
  // Se ignora la orientación EXIF: se mide la cuadrícula de píxeles tal como se guardó.
  const cv::Mat image = cv::imdecode(buffer, cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
  if (image.empty()) throw InvalidImageError("la imagen no se pudo decodificar como JPEG");

  PhotoRecord record;
  record.metadata = metadata;
  record.file = photoFileName(metadata.index);
  record.quality = evaluateQuality(image, thresholds_);
  if ((metadata.width && metadata.width != image.cols) || (metadata.height && metadata.height != image.rows)) {
    std::cerr << "[aviso] sesion " << id << ", foto " << metadata.index << ": la app declaro "
              << metadata.width << "x" << metadata.height << " pero la imagen es " << image.cols << "x"
              << image.rows << "; se usan las dimensiones reales\n";
  }
  record.metadata.width = image.cols;
  record.metadata.height = image.rows;

  std::lock_guard<std::mutex> lock(mutex_);
  Manifest& m = findOrThrow(id);
  writeFileAtomic(root_ / id / record.file, jpegBytes);
  auto it = std::find_if(m.photos.begin(), m.photos.end(),
                         [&](const PhotoRecord& p) { return p.metadata.index == metadata.index; });
  if (it != m.photos.end()) {
    *it = record;
  } else {
    m.photos.push_back(record);
    std::sort(m.photos.begin(), m.photos.end(),
              [](const PhotoRecord& a, const PhotoRecord& b) { return a.metadata.index < b.metadata.index; });
  }
  return record;
}

Summary SessionStore::complete(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Manifest& m = findOrThrow(id);
  writeManifestAtomic(root_ / id / "manifest.json", m);
  return summarize(m.photos);
}

Manifest SessionStore::manifest(const std::string& id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return findOrThrow(id);
}

Manifest& SessionStore::findOrThrow(const std::string& id) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) throw SessionNotFoundError("la sesión '" + id + "' no existe");
  return it->second;
}

const Manifest& SessionStore::findOrThrow(const std::string& id) const {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) throw SessionNotFoundError("la sesión '" + id + "' no existe");
  return it->second;
}
