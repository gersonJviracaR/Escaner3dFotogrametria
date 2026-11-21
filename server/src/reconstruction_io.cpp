#include "reconstruction_io.h"

#include "atomic_file.h"
#include "pairing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

using nlohmann::json;

namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

double rotationAngleDeg(const cv::Matx33d& R) {
  const double c = std::clamp((cv::trace(R) - 1.0) / 2.0, -1.0, 1.0);
  return std::acos(c) * kRadToDeg;
}

double meanError(const Reconstruction& rec, const Point3D& p) {
  double sum = 0;
  int n = 0;
  for (const Observation& o : p.observations) {
    const auto it = rec.cameras.find(o.image);
    if (it == rec.cameras.end() || !it->second.registered) continue;
    sum += reprojectionError(rec.camera, it->second.pose, p.xyz, o.xy);
    ++n;
  }
  return n > 0 ? sum / n : 0.0;
}

}  // namespace

json toJson(const ReconstructionFile& f) {
  const Reconstruction& rec = f.rec;
  json cameras = json::array();
  for (const auto& [img, cam] : rec.cameras) {
    json c{{"index", img}, {"registered", cam.registered}};
    if (cam.registered) {
      const cv::Vec3d C = cam.pose.center();
      c["reason"] = nullptr;
      c["q"] = json::array({cam.pose.q[0], cam.pose.q[1], cam.pose.q[2], cam.pose.q[3]});
      c["t"] = json::array({cam.pose.t[0], cam.pose.t[1], cam.pose.t[2]});
      c["center"] = json::array({C[0], C[1], C[2]});
    } else {
      c["reason"] = cam.reason;
      c["q"] = nullptr;
      c["t"] = nullptr;
      c["center"] = nullptr;
    }
    cameras.push_back(std::move(c));
  }
  json points = json::array();
  for (std::size_t k = 0; k < rec.points.size(); ++k) {
    const Point3D& p = rec.points[k];
    json obs = json::array();
    for (const Observation& o : p.observations) obs.push_back(json::array({o.image, o.point, o.xy.x, o.xy.y}));
    points.push_back(json{{"id", static_cast<int>(k)},
                          {"xyz", json::array({p.xyz[0], p.xyz[1], p.xyz[2]})},
                          {"rgb", json::array({p.rgb[0], p.rgb[1], p.rgb[2]})},
                          {"track_length", static_cast<int>(p.observations.size())},
                          {"error_px", meanError(rec, p)},
                          {"observations", std::move(obs)}});
  }
  return json{
      {"schema_version", kReconstructionSchemaVersion},
      {"session_id", f.session_id},
      {"generated_at", f.generated_at},
      {"conventions",
       {{"quaternion", "[w, x, y, z] Hamilton (como el manifiesto, COLMAP y Ceres)"},
        {"pose", "mundo -> cámara: x_cam = R·X + t; centro C = -Rᵀ·t"},
        {"camera_axes", "x a la derecha, y hacia abajo, z hacia adelante"},
        {"pixels", "imagen original sin orientación EXIF; centro del píxel (0,0) en (0,0)"},
        {"scale", "arbitraria (monocular): distancia 1 entre los centros del par inicial"},
        {"observations", "[foto, índice del punto en features/img_###.feat, x, y]"}}},
      {"params", toJson(f.params)},
      {"intrinsics",
       {{"model", "radial k1, k2; f compartida; punto principal fijo"},
        {"f", rec.camera.f},
        {"k1", rec.camera.k1},
        {"k2", rec.camera.k2},
        {"cx", rec.camera.cx},
        {"cy", rec.camera.cy},
        {"width", rec.camera.width},
        {"height", rec.camera.height}}},
      {"cameras", std::move(cameras)},
      {"points", std::move(points)}};
}

ReconstructionFile reconstructionFromJson(const json& j) {
  try {
    ReconstructionFile f;
    if (j.at("schema_version").get<int>() != kReconstructionSchemaVersion)
      throw std::invalid_argument("versión de reconstruction.json no soportada");
    f.session_id = j.at("session_id").get<std::string>();
    f.generated_at = j.at("generated_at").get<std::string>();
    f.params = applyReconstructionJson(ReconstructionParams{}, j.at("params"));
    const json& in = j.at("intrinsics");
    Reconstruction& rec = f.rec;
    rec.camera.f = in.at("f").get<double>();
    rec.camera.k1 = in.at("k1").get<double>();
    rec.camera.k2 = in.at("k2").get<double>();
    rec.camera.cx = in.at("cx").get<double>();
    rec.camera.cy = in.at("cy").get<double>();
    rec.camera.width = in.at("width").get<int>();
    rec.camera.height = in.at("height").get<int>();
    for (const json& c : j.at("cameras")) {
      Camera cam;
      cam.index = c.at("index").get<int>();
      cam.registered = c.at("registered").get<bool>();
      if (cam.registered) {
        const json& q = c.at("q");
        const json& t = c.at("t");
        cam.pose.q = {q.at(0).get<double>(), q.at(1).get<double>(), q.at(2).get<double>(), q.at(3).get<double>()};
        cam.pose.t = cv::Vec3d(t.at(0).get<double>(), t.at(1).get<double>(), t.at(2).get<double>());
      } else {
        cam.reason = c.at("reason").get<std::string>();
      }
      rec.cameras[cam.index] = cam;
    }
    for (const json& pj : j.at("points")) {
      Point3D p;
      const json& x = pj.at("xyz");
      p.xyz = cv::Vec3d(x.at(0).get<double>(), x.at(1).get<double>(), x.at(2).get<double>());
      const json& c = pj.at("rgb");
      p.rgb = cv::Vec3b(c.at(0).get<uchar>(), c.at(1).get<uchar>(), c.at(2).get<uchar>());
      for (const json& o : pj.at("observations"))
        p.observations.push_back(
            {o.at(0).get<int>(), o.at(1).get<int>(), cv::Point2d(o.at(2).get<double>(), o.at(3).get<double>())});
      rec.points.push_back(std::move(p));
    }
    return f;
  } catch (const json::exception& e) {
    throw std::invalid_argument(std::string("reconstruction.json inválido: ") + e.what());
  }
}

void writeReconstruction(const std::filesystem::path& file, const ReconstructionFile& f) {
  writeFileAtomic(file, toJson(f).dump(1) + '\n');
}

ReconstructionFile readReconstruction(const std::filesystem::path& file) {
  return reconstructionFromJson(json::parse(readFileBytes(file)));
}

std::string toPly(const Reconstruction& rec) {
  const int cameras = rec.registeredCount();
  std::string out = "ply\nformat ascii 1.0\n";
  out += "comment Escaner 3D - nube dispersa (Sprint 3). Los ultimos " + std::to_string(cameras) +
         " vertices (magenta) son centros de camara.\n";
  out += "element vertex " + std::to_string(rec.points.size() + static_cast<std::size_t>(cameras)) + "\n";
  out +=
      "property float x\nproperty float y\nproperty float z\n"
      "property uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n";
  char line[160];
  for (const Point3D& p : rec.points) {
    std::snprintf(line, sizeof(line), "%.7g %.7g %.7g %d %d %d\n", p.xyz[0], p.xyz[1], p.xyz[2], p.rgb[0], p.rgb[1],
                  p.rgb[2]);
    out += line;
  }
  for (const auto& [img, cam] : rec.cameras) {
    if (!cam.registered) continue;
    const cv::Vec3d C = cam.pose.center();
    std::snprintf(line, sizeof(line), "%.7g %.7g %.7g 255 0 255\n", C[0], C[1], C[2]);
    out += line;
  }
  return out;
}

void writePly(const std::filesystem::path& file, const Reconstruction& rec) { writeFileAtomic(file, toPly(rec)); }

json imuConsistencyJson(const Reconstruction& rec, const std::map<int, Quat>& imu) {
  std::vector<int> regs;
  for (const auto& [img, cam] : rec.cameras)
    if (cam.registered && imu.count(img)) regs.push_back(img);
  json pairs = json::array();
  double sum = 0, worst = 0;
  for (std::size_t k = 0; k + 1 < regs.size(); ++k) {
    const int i = regs[k], j = regs[k + 1];
    const double rec_deg = rotationAngleDeg(rec.cameras.at(j).pose.R() * rec.cameras.at(i).pose.R().t());
    const double imu_deg = quaternionAngleDeg(imu.at(i), imu.at(j));
    const double diff = rec_deg - imu_deg;
    pairs.push_back(json{{"i", i}, {"j", j}, {"reconstruction_deg", rec_deg}, {"imu_deg", imu_deg}, {"diff_deg", diff}});
    sum += std::abs(diff);
    worst = std::max(worst, std::abs(diff));
  }
  return json{{"pairs", pairs},
              {"mean_abs_diff_deg", pairs.empty() ? 0.0 : sum / static_cast<double>(pairs.size())},
              {"max_abs_diff_deg", worst}};
}

json ringGeometryJson(const Reconstruction& rec) {
  std::vector<cv::Vec3d> centers;
  for (const auto& [img, cam] : rec.cameras)
    if (cam.registered) centers.push_back(cam.pose.center());
  if (centers.empty()) return json{{"cameras", 0}};
  cv::Vec3d c(0, 0, 0);
  for (const cv::Vec3d& x : centers) c += x;
  c *= 1.0 / static_cast<double>(centers.size());
  std::vector<double> d;
  for (const cv::Vec3d& x : centers) d.push_back(cv::norm(x - c));
  double mean = 0, var = 0;
  for (double v : d) mean += v;
  mean /= static_cast<double>(d.size());
  for (double v : d) var += (v - mean) * (v - mean);
  var /= static_cast<double>(d.size());
  return json{{"cameras", static_cast<int>(centers.size())},
              {"centroid", json::array({c[0], c[1], c[2]})},
              {"mean_distance", mean},
              {"std_distance", std::sqrt(var)},
              {"min_distance", *std::min_element(d.begin(), d.end())},
              {"max_distance", *std::max_element(d.begin(), d.end())}};
}
