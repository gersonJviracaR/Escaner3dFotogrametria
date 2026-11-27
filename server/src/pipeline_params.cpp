#include "pipeline_params.h"

#include <set>
#include <stdexcept>

using nlohmann::json;

namespace {

void checkKeys(const json& j, const char* section, const std::set<std::string>& known) {
  if (!j.is_object()) throw std::invalid_argument(std::string("la sección ") + section + " debe ser un objeto JSON");
  for (const auto& item : j.items())
    if (!known.count(item.key()))
      throw std::invalid_argument(std::string("clave desconocida en ") + section + ": " + item.key());
}

int intIn(const json& j, const char* section, const char* key, long long lo, long long hi) {
  const json& v = j.at(key);
  if (!v.is_number_integer() || v.get<long long>() < lo || v.get<long long>() > hi)
    throw std::invalid_argument(std::string(section) + "." + key + " debe ser un entero entre " + std::to_string(lo) +
                                " y " + std::to_string(hi));
  return v.get<int>();
}

double numberIn(const json& j, const char* section, const char* key, double lo, double hi) {
  const json& v = j.at(key);
  if (!v.is_number() || v.get<double>() < lo || v.get<double>() > hi)
    throw std::invalid_argument(std::string(section) + "." + key + " fuera de rango");
  return v.get<double>();
}

bool boolean(const json& j, const char* section, const char* key) {
  if (!j.at(key).is_boolean()) throw std::invalid_argument(std::string(section) + "." + key + " debe ser true o false");
  return j.at(key).get<bool>();
}

}  // namespace

DenseParams applyDenseJson(DenseParams p, const json& j) {
  checkKeys(j, "dense", {"colmap_path", "max_image_size", "geom_consistency"});
  if (j.contains("colmap_path")) {
    if (!j.at("colmap_path").is_string()) throw std::invalid_argument("dense.colmap_path debe ser texto");
    p.colmap_path = j.at("colmap_path").get<std::string>();
  }
  if (j.contains("max_image_size")) p.max_image_size = intIn(j, "dense", "max_image_size", 200, 20000);
  if (j.contains("geom_consistency")) p.geom_consistency = boolean(j, "dense", "geom_consistency");
  return p;
}

MeshParams applyMeshJson(MeshParams p, const json& j) {
  checkKeys(j, "mesh", {"poisson_depth", "trim_density", "min_component_faces"});
  if (j.contains("poisson_depth")) p.poisson_depth = intIn(j, "mesh", "poisson_depth", 4, 16);
  if (j.contains("trim_density")) p.trim_density = numberIn(j, "mesh", "trim_density", 0, 100);
  if (j.contains("min_component_faces")) p.min_component_faces = intIn(j, "mesh", "min_component_faces", 0, 100000000);
  return p;
}

TextureParams applyTextureJson(TextureParams p, const json& j) {
  checkKeys(j, "texture", {"min_cos", "atlas_max_size", "depth_map_long_side", "padding_px"});
  if (j.contains("min_cos")) p.min_cos = numberIn(j, "texture", "min_cos", 0, 1);
  if (j.contains("atlas_max_size")) p.atlas_max_size = intIn(j, "texture", "atlas_max_size", 256, 16384);
  if (j.contains("depth_map_long_side")) p.depth_map_long_side = intIn(j, "texture", "depth_map_long_side", 100, 8000);
  if (j.contains("padding_px")) p.padding_px = intIn(j, "texture", "padding_px", 0, 32);
  return p;
}

ViewerParams applyViewerJson(ViewerParams p, const json& j) {
  checkKeys(j, "viewer", {"width", "height", "max_triangles_interactive"});
  if (j.contains("width")) p.width = intIn(j, "viewer", "width", 100, 8000);
  if (j.contains("height")) p.height = intIn(j, "viewer", "height", 100, 8000);
  if (j.contains("max_triangles_interactive"))
    p.max_triangles_interactive = intIn(j, "viewer", "max_triangles_interactive", 100, 100000000);
  return p;
}

AccuracyParams applyAccuracyJson(AccuracyParams p, const json& j) {
  checkKeys(j, "accuracy", {"min_common_cameras", "ransac_iterations"});
  if (j.contains("min_common_cameras")) p.min_common_cameras = intIn(j, "accuracy", "min_common_cameras", 3, 100000);
  if (j.contains("ransac_iterations")) p.ransac_iterations = intIn(j, "accuracy", "ransac_iterations", 1, 1000000);
  return p;
}

json toJson(const DenseParams& p) {
  return json{{"colmap_path", p.colmap_path}, {"max_image_size", p.max_image_size}, {"geom_consistency", p.geom_consistency}};
}
json toJson(const MeshParams& p) {
  return json{{"poisson_depth", p.poisson_depth}, {"trim_density", p.trim_density},
              {"min_component_faces", p.min_component_faces}};
}
json toJson(const TextureParams& p) {
  return json{{"min_cos", p.min_cos}, {"atlas_max_size", p.atlas_max_size},
              {"depth_map_long_side", p.depth_map_long_side}, {"padding_px", p.padding_px}};
}
json toJson(const ViewerParams& p) {
  return json{{"width", p.width}, {"height", p.height}, {"max_triangles_interactive", p.max_triangles_interactive}};
}
json toJson(const AccuracyParams& p) {
  return json{{"min_common_cameras", p.min_common_cameras}, {"ransac_iterations", p.ransac_iterations}};
}
