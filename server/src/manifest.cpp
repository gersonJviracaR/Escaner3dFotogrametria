#include "manifest.h"

#include "atomic_file.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

using nlohmann::json;

namespace {

std::optional<double> optionalNumber(const json& j, const char* key) {
  if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
  if (!j.at(key).is_number()) throw MetadataError(std::string("'") + key + "' debe ser numérico o null");
  return j.at(key).get<double>();
}

int optionalNonNegativeInt(const json& j, const char* key) {
  if (!j.contains(key) || j.at(key).is_null()) return 0;
  if (!j.at(key).is_number_integer() || j.at(key).get<long long>() < 0)
    throw MetadataError(std::string("'") + key + "' debe ser un entero no negativo");
  return j.at(key).get<int>();
}

json optionalToJson(const std::optional<double>& v) { return v ? json(*v) : json(nullptr); }

}  // namespace

std::string photoFileName(int index) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "img_%03d.jpg", index);
  return buffer;
}

PhotoMetadata parsePhotoMetadata(const json& j) {
  if (!j.is_object()) throw MetadataError("los metadatos deben ser un objeto JSON");

  PhotoMetadata m;
  if (!j.contains("index") || !j.at("index").is_number_integer())
    throw MetadataError("'index' es obligatorio y debe ser entero");
  const long long index = j.at("index").get<long long>();
  if (index < 1 || index > kMaxPhotoIndex)
    throw MetadataError("'index' debe estar entre 1 y " + std::to_string(kMaxPhotoIndex));
  m.index = static_cast<int>(index);

  if (j.contains("captured_at") && !j.at("captured_at").is_null()) {
    if (!j.at("captured_at").is_string()) throw MetadataError("'captured_at' debe ser texto");
    m.captured_at = j.at("captured_at").get<std::string>();
  }
  m.width = optionalNonNegativeInt(j, "width");
  m.height = optionalNonNegativeInt(j, "height");
  m.focal_mm = optionalNumber(j, "focal_mm");
  m.focal_35mm = optionalNumber(j, "focal_35mm");

  if (j.contains("orientation_q") && !j.at("orientation_q").is_null()) {
    const json& q = j.at("orientation_q");
    if (!q.is_array() || q.size() != 4 ||
        !std::all_of(q.begin(), q.end(), [](const json& v) { return v.is_number(); }))
      throw MetadataError("'orientation_q' debe ser un arreglo de 4 números [w, x, y, z]");
    m.orientation_q = std::array<double, 4>{q[0].get<double>(), q[1].get<double>(),
                                            q[2].get<double>(), q[3].get<double>()};
  }
  return m;
}

json toJson(const PhotoMetadata& m) {
  return json{{"index", m.index},
              {"captured_at", m.captured_at},
              {"width", m.width},
              {"height", m.height},
              {"focal_mm", optionalToJson(m.focal_mm)},
              {"focal_35mm", optionalToJson(m.focal_35mm)},
              {"orientation_q", m.orientation_q ? json(*m.orientation_q) : json(nullptr)}};
}

json toJson(const QualityResult& q) {
  return json{{"sharpness", q.sharpness},
              {"overexposed_ratio", q.overexposed_ratio},
              {"accepted", q.accepted},
              {"reasons", q.reasons}};
}

json validationJson(const PhotoRecord& photo) {
  json j = toJson(photo.quality);
  j["index"] = photo.metadata.index;
  return j;
}

Summary summarize(const std::vector<PhotoRecord>& photos) {
  Summary s;
  s.total = static_cast<int>(photos.size());
  for (const PhotoRecord& p : photos) {
    if (p.quality.accepted)
      ++s.accepted;
    else
      s.rejected.push_back(p.metadata.index);
  }
  std::sort(s.rejected.begin(), s.rejected.end());
  return s;
}

json toJson(const Summary& s) {
  return json{{"total", s.total}, {"accepted", s.accepted}, {"rejected", s.rejected}};
}

json toJson(const Manifest& m) {
  json photos = json::array();
  for (const PhotoRecord& p : m.photos) {
    json pj = toJson(p.metadata);
    pj["file"] = p.file;
    pj["quality"] = toJson(p.quality);
    photos.push_back(std::move(pj));
  }
  return json{{"schema_version", m.schema_version},
              {"session_id", m.session_id},
              {"created_at", m.created_at},
              {"device", m.device},
              {"photos", std::move(photos)},
              {"summary", toJson(summarize(m.photos))}};
}

Manifest manifestFromJson(const json& j) {
  Manifest m;
  m.schema_version = j.at("schema_version").get<int>();
  m.session_id = j.at("session_id").get<std::string>();
  m.created_at = j.at("created_at").get<std::string>();
  m.device = j.value("device", json::object());
  for (const json& pj : j.at("photos")) {
    PhotoRecord p;
    p.metadata = parsePhotoMetadata(pj);
    p.file = pj.at("file").get<std::string>();
    const json& q = pj.at("quality");
    p.quality.sharpness = q.at("sharpness").get<double>();
    p.quality.overexposed_ratio = q.at("overexposed_ratio").get<double>();
    p.quality.accepted = q.at("accepted").get<bool>();
    p.quality.reasons = q.at("reasons").get<std::vector<std::string>>();
    m.photos.push_back(std::move(p));
  }
  return m;
}

void writeManifestAtomic(const std::filesystem::path& file, const Manifest& m) {
  writeFileAtomic(file, toJson(m).dump(2) + '\n');
}

Manifest readManifest(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("no se pudo leer " + file.string());
  return manifestFromJson(json::parse(in));
}
