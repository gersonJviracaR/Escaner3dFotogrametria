#include "matching_params.h"

#include <set>
#include <stdexcept>
#include <string>

using nlohmann::json;

namespace {

double numberIn(const json& j, const char* key, double lo, double hi, bool lo_open, bool hi_open) {
  const json& v = j.at(key);
  if (!v.is_number()) throw std::invalid_argument(std::string("matching.") + key + " debe ser numérico");
  const double x = v.get<double>();
  const bool lo_ok = lo_open ? x > lo : x >= lo;
  const bool hi_ok = hi_open ? x < hi : x <= hi;
  if (!lo_ok || !hi_ok) throw std::invalid_argument(std::string("matching.") + key + " fuera de rango");
  return x;
}

int positiveInt(const json& j, const char* key) {
  const json& v = j.at(key);
  if (!v.is_number_integer() || v.get<long long>() < 1 || v.get<long long>() > 1000000)
    throw std::invalid_argument(std::string("matching.") + key + " debe ser un entero positivo");
  return v.get<int>();
}

const std::set<std::string> kKnownKeys = {
    "work_long_side",      "max_features",      "clahe",       "clahe_clip_limit", "clahe_tile",
    "ratio",               "max_angle_deg",     "max_neighbors", "ransac_threshold_px",
    "ransac_confidence",   "min_inliers",       "min_inlier_ratio", "fallback_focal_factor",
    "neighbor_index_window", "ring_closure"};

std::string keyFromOption(const std::string& option) {
  if (option.rfind("--", 0) != 0) return {};
  std::string key = option.substr(2);
  for (char& ch : key)
    if (ch == '-') ch = '_';
  return kKnownKeys.count(key) ? key : std::string();
}

}  // namespace

MatchingParams applyMatchingJson(MatchingParams p, const json& j) {
  if (!j.is_object()) throw std::invalid_argument("la sección matching debe ser un objeto JSON");
  for (const auto& item : j.items())
    if (!kKnownKeys.count(item.key())) throw std::invalid_argument("clave desconocida en matching: " + item.key());

  constexpr double kInf = 1e300;
  if (j.contains("work_long_side")) p.work_long_side = positiveInt(j, "work_long_side");
  if (j.contains("max_features")) p.max_features = positiveInt(j, "max_features");
  if (j.contains("clahe")) {
    if (!j.at("clahe").is_boolean()) throw std::invalid_argument("matching.clahe debe ser true o false");
    p.clahe = j.at("clahe").get<bool>();
  }
  if (j.contains("clahe_clip_limit")) p.clahe_clip_limit = numberIn(j, "clahe_clip_limit", 0, kInf, true, false);
  if (j.contains("clahe_tile")) p.clahe_tile = positiveInt(j, "clahe_tile");
  if (j.contains("ratio")) p.ratio = numberIn(j, "ratio", 0, 1, true, false);
  if (j.contains("max_angle_deg")) p.max_angle_deg = numberIn(j, "max_angle_deg", 0, 180, true, false);
  if (j.contains("max_neighbors")) p.max_neighbors = positiveInt(j, "max_neighbors");
  if (j.contains("neighbor_index_window")) {
    const json& v = j.at("neighbor_index_window");
    if (!v.is_number_integer() || v.get<long long>() < 0 || v.get<long long>() > 10)
      throw std::invalid_argument("matching.neighbor_index_window debe ser un entero entre 0 y 10");
    p.neighbor_index_window = v.get<int>();
  }
  if (j.contains("ring_closure")) {
    if (!j.at("ring_closure").is_boolean()) throw std::invalid_argument("matching.ring_closure debe ser true o false");
    p.ring_closure = j.at("ring_closure").get<bool>();
  }
  if (j.contains("ransac_threshold_px"))
    p.ransac_threshold_px = numberIn(j, "ransac_threshold_px", 0, kInf, true, false);
  if (j.contains("ransac_confidence")) p.ransac_confidence = numberIn(j, "ransac_confidence", 0, 1, true, true);
  if (j.contains("min_inliers")) p.min_inliers = positiveInt(j, "min_inliers");
  if (j.contains("min_inlier_ratio")) p.min_inlier_ratio = numberIn(j, "min_inlier_ratio", 0, 1, false, false);
  if (j.contains("fallback_focal_factor"))
    p.fallback_focal_factor = numberIn(j, "fallback_focal_factor", 0, kInf, true, false);
  return p;
}

json toJson(const MatchingParams& p) {
  return json{{"work_long_side", p.work_long_side},
              {"max_features", p.max_features},
              {"clahe", p.clahe},
              {"clahe_clip_limit", p.clahe_clip_limit},
              {"clahe_tile", p.clahe_tile},
              {"ratio", p.ratio},
              {"max_angle_deg", p.max_angle_deg},
              {"max_neighbors", p.max_neighbors},
              {"neighbor_index_window", p.neighbor_index_window},
              {"ring_closure", p.ring_closure},
              {"ransac_threshold_px", p.ransac_threshold_px},
              {"ransac_confidence", p.ransac_confidence},
              {"min_inliers", p.min_inliers},
              {"min_inlier_ratio", p.min_inlier_ratio},
              {"fallback_focal_factor", p.fallback_focal_factor}};
}

bool isMatchingOverride(const std::string& option) { return !keyFromOption(option).empty(); }

MatchingParams applyMatchingOverride(MatchingParams base, const std::string& option, const std::string& value) {
  const std::string key = keyFromOption(option);
  if (key.empty()) throw std::invalid_argument("opción desconocida: " + option);
  json v;
  if (value == "true" || value == "false") {
    v = (value == "true");
  } else {
    v = json::parse(value, nullptr, /*allow_exceptions=*/false);
    if (!v.is_number()) throw std::invalid_argument("valor inválido para " + option + ": " + value);
  }
  return applyMatchingJson(base, json{{key, v}});
}