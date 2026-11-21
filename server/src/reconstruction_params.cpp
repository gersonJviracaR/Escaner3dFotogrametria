#include "reconstruction_params.h"

#include <set>
#include <stdexcept>

using nlohmann::json;

namespace {

const std::set<std::string> kKnownKeys = {
    "exclude_rejected_photos", "init_min_inliers",     "init_min_median_triangulation_angle_deg",
    "tri_min_angle_deg",       "max_reproj_error_px",  "pnp_ransac_threshold_px",
    "pnp_confidence",          "pnp_min_inliers",      "min_views",
    "ba_every",                "ba_max_iterations",    "huber_delta_px",
    "refine_focal",            "refine_distortion"};

std::string name(const char* key) { return std::string("reconstruction.") + key; }

double numberIn(const json& j, const char* key, double lo, double hi, bool lo_open, bool hi_open) {
  const json& v = j.at(key);
  if (!v.is_number()) throw std::invalid_argument(name(key) + " debe ser numérico");
  const double x = v.get<double>();
  const bool lo_ok = lo_open ? x > lo : x >= lo;
  const bool hi_ok = hi_open ? x < hi : x <= hi;
  if (!lo_ok || !hi_ok) throw std::invalid_argument(name(key) + " fuera de rango");
  return x;
}

int intAtLeast(const json& j, const char* key, long long lo) {
  const json& v = j.at(key);
  if (!v.is_number_integer() || v.get<long long>() < lo || v.get<long long>() > 1000000)
    throw std::invalid_argument(name(key) + " debe ser un entero >= " + std::to_string(lo));
  return v.get<int>();
}

bool boolean(const json& j, const char* key) {
  if (!j.at(key).is_boolean()) throw std::invalid_argument(name(key) + " debe ser true o false");
  return j.at(key).get<bool>();
}

std::string keyFromOption(const std::string& option) {
  if (option.rfind("--", 0) != 0) return {};
  std::string key = option.substr(2);
  for (char& ch : key)
    if (ch == '-') ch = '_';
  return kKnownKeys.count(key) ? key : std::string();
}

}  // namespace

ReconstructionParams applyReconstructionJson(ReconstructionParams p, const json& j) {
  if (!j.is_object()) throw std::invalid_argument("la sección reconstruction debe ser un objeto JSON");
  for (const auto& item : j.items())
    if (!kKnownKeys.count(item.key()))
      throw std::invalid_argument("clave desconocida en reconstruction: " + item.key());

  constexpr double kInf = 1e300;
  if (j.contains("exclude_rejected_photos")) p.exclude_rejected_photos = boolean(j, "exclude_rejected_photos");
  if (j.contains("init_min_inliers")) p.init_min_inliers = intAtLeast(j, "init_min_inliers", 8);
  if (j.contains("init_min_median_triangulation_angle_deg"))
    p.init_min_median_triangulation_angle_deg =
        numberIn(j, "init_min_median_triangulation_angle_deg", 0, 90, false, true);
  if (j.contains("tri_min_angle_deg")) p.tri_min_angle_deg = numberIn(j, "tri_min_angle_deg", 0, 90, false, true);
  if (j.contains("max_reproj_error_px"))
    p.max_reproj_error_px = numberIn(j, "max_reproj_error_px", 0, kInf, true, false);
  if (j.contains("pnp_ransac_threshold_px"))
    p.pnp_ransac_threshold_px = numberIn(j, "pnp_ransac_threshold_px", 0, kInf, true, false);
  if (j.contains("pnp_confidence")) p.pnp_confidence = numberIn(j, "pnp_confidence", 0, 1, true, true);
  if (j.contains("pnp_min_inliers")) p.pnp_min_inliers = intAtLeast(j, "pnp_min_inliers", 4);
  if (j.contains("min_views")) p.min_views = intAtLeast(j, "min_views", 2);
  if (j.contains("ba_every")) p.ba_every = intAtLeast(j, "ba_every", 1);
  if (j.contains("ba_max_iterations")) p.ba_max_iterations = intAtLeast(j, "ba_max_iterations", 1);
  if (j.contains("huber_delta_px")) p.huber_delta_px = numberIn(j, "huber_delta_px", 0, kInf, true, false);
  if (j.contains("refine_focal")) p.refine_focal = boolean(j, "refine_focal");
  if (j.contains("refine_distortion")) p.refine_distortion = boolean(j, "refine_distortion");
  return p;
}

json toJson(const ReconstructionParams& p) {
  return json{{"exclude_rejected_photos", p.exclude_rejected_photos},
              {"init_min_inliers", p.init_min_inliers},
              {"init_min_median_triangulation_angle_deg", p.init_min_median_triangulation_angle_deg},
              {"tri_min_angle_deg", p.tri_min_angle_deg},
              {"max_reproj_error_px", p.max_reproj_error_px},
              {"pnp_ransac_threshold_px", p.pnp_ransac_threshold_px},
              {"pnp_confidence", p.pnp_confidence},
              {"pnp_min_inliers", p.pnp_min_inliers},
              {"min_views", p.min_views},
              {"ba_every", p.ba_every},
              {"ba_max_iterations", p.ba_max_iterations},
              {"huber_delta_px", p.huber_delta_px},
              {"refine_focal", p.refine_focal},
              {"refine_distortion", p.refine_distortion}};
}

bool isReconstructionOverride(const std::string& option) { return !keyFromOption(option).empty(); }

ReconstructionParams applyReconstructionOverride(ReconstructionParams base, const std::string& option,
                                                 const std::string& value) {
  const std::string key = keyFromOption(option);
  if (key.empty()) throw std::invalid_argument("opción desconocida: " + option);
  json v;
  if (value == "true" || value == "false") {
    v = (value == "true");
  } else {
    v = json::parse(value, nullptr, /*allow_exceptions=*/false);
    if (!v.is_number()) throw std::invalid_argument("valor inválido para " + option + ": " + value);
  }
  return applyReconstructionJson(base, json{{key, v}});
}
