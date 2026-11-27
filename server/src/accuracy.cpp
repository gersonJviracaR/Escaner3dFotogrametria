#include "accuracy.h"

#include "atomic_file.h"
#include "colmap_io.h"
#include "reconstruction_report.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace fs = std::filesystem;
using nlohmann::json;

Similarity3 umeyama(const std::vector<cv::Vec3d>& from, const std::vector<cv::Vec3d>& to) {
  const double n = static_cast<double>(from.size());
  cv::Vec3d mf(0, 0, 0), mt(0, 0, 0);
  for (std::size_t k = 0; k < from.size(); ++k) {
    mf += from[k];
    mt += to[k];
  }
  mf *= 1.0 / n;
  mt *= 1.0 / n;
  cv::Matx33d cov = cv::Matx33d::zeros();
  double var = 0;
  for (std::size_t k = 0; k < from.size(); ++k) {
    const cv::Vec3d a = from[k] - mf, b = to[k] - mt;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c) cov(r, c) += b[r] * a[c] / n;
    var += a.dot(a) / n;
  }
  cv::Matx33d U, Vt;
  cv::Matx31d D;
  cv::SVD::compute(cov, D, U, Vt);
  cv::Matx33d S = cv::Matx33d::eye();
  if (cv::determinant(U * Vt) < 0) S(2, 2) = -1;
  Similarity3 sim;
  sim.R = U * S * Vt;
  sim.s = var > 0 ? (D(0) * S(0, 0) + D(1) * S(1, 1) + D(2) * S(2, 2)) / var : 1.0;
  sim.t = mt - sim.s * (sim.R * mf);
  return sim;
}

Similarity3 umeyamaRansac(const std::vector<cv::Vec3d>& from, const std::vector<cv::Vec3d>& to, int iterations,
                          double threshold, std::vector<int>& inliers, unsigned seed) {
  const int n = static_cast<int>(from.size());
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> pick(0, n - 1);
  auto countInliers = [&](const Similarity3& s, std::vector<int>& out) {
    out.clear();
    for (int k = 0; k < n; ++k)
      if (cv::norm(s.apply(from[static_cast<std::size_t>(k)]) - to[static_cast<std::size_t>(k)]) < threshold) out.push_back(k);
  };
  std::vector<int> best, current;
  for (int it = 0; it < iterations && n >= 3; ++it) {
    const int a = pick(rng), b = pick(rng), c = pick(rng);
    if (a == b || b == c || a == c) continue;
    const Similarity3 s = umeyama({from[static_cast<std::size_t>(a)], from[static_cast<std::size_t>(b)], from[static_cast<std::size_t>(c)]},
                                  {to[static_cast<std::size_t>(a)], to[static_cast<std::size_t>(b)], to[static_cast<std::size_t>(c)]});
    countInliers(s, current);
    if (current.size() > best.size()) best = current;
  }
  if (best.size() < 3) {  // sin consenso: se usa todo
    inliers.resize(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) inliers[static_cast<std::size_t>(k)] = k;
    return umeyama(from, to);
  }
  std::vector<cv::Vec3d> f, t;
  for (int k : best) {
    f.push_back(from[static_cast<std::size_t>(k)]);
    t.push_back(to[static_cast<std::size_t>(k)]);
  }
  const Similarity3 refined = umeyama(f, t);
  countInliers(refined, inliers);
  return refined;
}

double rotationDifferenceDeg(const cv::Matx33d& R1, const cv::Matx33d& R2) {
  const double c = std::clamp((cv::trace(R1 * R2.t()) - 1.0) / 2.0, -1.0, 1.0);
  return std::acos(c) * 180.0 / 3.14159265358979323846;
}

AccuracyReport compareReconstructions(const Reconstruction& ours, const Reconstruction& colmap,
                                      const AccuracyParams& params) {
  AccuracyReport r;
  r.ours_registered = ours.registeredCount();
  r.colmap_registered = colmap.registeredCount();
  r.f_ours = ours.camera.f;
  r.f_colmap = colmap.camera.f;
  r.k1_ours = ours.camera.k1, r.k2_ours = ours.camera.k2;
  r.k1_colmap = colmap.camera.k1, r.k2_colmap = colmap.camera.k2;
  const CameraErrors eo = computeCameraErrors(ours), ec = computeCameraErrors(colmap);
  r.reproj_mean_ours = eo.global.mean, r.reproj_rms_ours = eo.global.rms;
  r.reproj_mean_colmap = ec.global.mean, r.reproj_rms_colmap = ec.global.rms;
  r.points_ours = static_cast<int>(ours.points.size());
  r.points_colmap = static_cast<int>(colmap.points.size());

  std::vector<int> common;
  for (const auto& [index, cam] : ours.cameras) {
    const auto it = colmap.cameras.find(index);
    if (cam.registered && it != colmap.cameras.end() && it->second.registered) common.push_back(index);
  }
  r.common = static_cast<int>(common.size());
  r.reliable = r.common >= params.min_common_cameras;
  if (!r.reliable) {
    r.note = "solo " + std::to_string(r.common) + " cámaras comunes (mínimo " + std::to_string(params.min_common_cameras) +
             "): la alineación no es fiable y no se reportan errores de pose";
    return r;
  }

  std::vector<cv::Vec3d> from, to;
  for (int index : common) {
    from.push_back(ours.cameras.at(index).pose.center());
    to.push_back(colmap.cameras.at(index).pose.center());
  }
  cv::Vec3d centroid(0, 0, 0);
  for (const cv::Vec3d& x : to) centroid += x;
  centroid *= 1.0 / static_cast<double>(to.size());
  double spread2 = 0;
  for (const cv::Vec3d& x : to) {
    r.ring_radius += cv::norm(x - centroid);
    spread2 += (x - centroid).dot(x - centroid);
  }
  r.ring_radius /= static_cast<double>(to.size());
  const double spread = std::sqrt(spread2 / static_cast<double>(to.size()));

  std::vector<int> inliers;
  Similarity3 sim;
  if (r.common > 8) {
    sim = umeyamaRansac(from, to, params.ransac_iterations, 0.05 * spread, inliers);
    r.note = "similitud con RANSAC (umbral 5 % de la dispersión de los centros de COLMAP)";
  } else {
    sim = umeyama(from, to);
    for (int k = 0; k < r.common; ++k) inliers.push_back(k);
    r.note = "similitud por mínimos cuadrados (8 o menos cámaras comunes, sin RANSAC)";
  }
  r.scale = sim.s;
  r.alignment_inliers = static_cast<int>(inliers.size());

  double sum = 0, sum2 = 0, rot_sum = 0;
  for (std::size_t k = 0; k < common.size(); ++k) {
    CameraAccuracy c;
    c.index = common[k];
    c.position_error = cv::norm(sim.apply(from[k]) - to[k]);
    // R_cam en el mundo de COLMAP ≈ R_propia · Rᵀ de la similitud.
    c.rotation_error_deg =
        rotationDifferenceDeg(colmap.cameras.at(c.index).pose.R(), ours.cameras.at(c.index).pose.R() * sim.R.t());
    c.inlier = std::find(inliers.begin(), inliers.end(), static_cast<int>(k)) != inliers.end();
    sum += c.position_error;
    sum2 += c.position_error * c.position_error;
    r.position_max = std::max(r.position_max, c.position_error);
    rot_sum += c.rotation_error_deg;
    r.rotation_max_deg = std::max(r.rotation_max_deg, c.rotation_error_deg);
    r.cameras.push_back(c);
  }
  r.position_mean = sum / r.common;
  r.position_rms = std::sqrt(sum2 / r.common);
  r.rotation_mean_deg = rot_sum / r.common;

  if (!ours.points.empty() && !colmap.points.empty()) {
    std::vector<double> d;
    d.reserve(ours.points.size());
    for (const Point3D& p : ours.points) {
      const cv::Vec3d x = sim.apply(p.xyz);
      double best = std::numeric_limits<double>::max();
      for (const Point3D& q : colmap.points) best = std::min(best, cv::norm(x - q.xyz));
      d.push_back(best);
    }
    const Stats s = describe(d);
    r.point_distance_mean = s.mean;
    r.point_distance_median = s.median;
  }
  return r;
}

json toJson(const AccuracyReport& r) {
  const bool ok = r.reliable;
  auto pct = [&](double v) { return r.ring_radius > 0 ? json(100.0 * v / r.ring_radius) : json(nullptr); };
  json per_camera = json::array();
  for (const CameraAccuracy& c : r.cameras)
    per_camera.push_back(json{{"index", c.index},
                              {"position_error", c.position_error},
                              {"rotation_error_deg", c.rotation_error_deg},
                              {"alignment_inlier", c.inlier}});
  return json{
      {"cameras_ours", r.ours_registered},
      {"cameras_colmap", r.colmap_registered},
      {"cameras_common", r.common},
      {"reliable", r.reliable},
      {"note", r.note},
      {"alignment",
       {{"scale", ok ? json(r.scale) : json(nullptr)},
        {"ring_radius", ok ? json(r.ring_radius) : json(nullptr)},
        {"inliers", ok ? json(r.alignment_inliers) : json(nullptr)},
        {"position_error",
         ok ? json{{"mean", r.position_mean}, {"rms", r.position_rms}, {"max", r.position_max}} : json(nullptr)},
        {"position_error_pct_radius",
         ok ? json{{"mean", pct(r.position_mean)}, {"rms", pct(r.position_rms)}, {"max", pct(r.position_max)}}
            : json(nullptr)},
        {"rotation_error_deg", ok ? json{{"mean", r.rotation_mean_deg}, {"max", r.rotation_max_deg}} : json(nullptr)}}},
      {"intrinsics",
       {{"f_ours", r.f_ours},
        {"f_colmap", r.f_colmap},
        {"f_diff_px", r.f_ours - r.f_colmap},
        {"f_diff_pct", r.f_colmap != 0 ? json(100.0 * (r.f_ours - r.f_colmap) / r.f_colmap) : json(nullptr)},
        {"k1_ours", r.k1_ours},
        {"k2_ours", r.k2_ours},
        {"k1_colmap", r.k1_colmap},
        {"k2_colmap", r.k2_colmap}}},
      {"reprojection_error_px",
       {{"ours", {{"mean", r.reproj_mean_ours}, {"rms", r.reproj_rms_ours}}},
        {"colmap", {{"mean", r.reproj_mean_colmap}, {"rms", r.reproj_rms_colmap}}}}},
      {"sparse_points",
       {{"ours", r.points_ours},
        {"colmap", r.points_colmap},
        {"distance_to_nearest_colmap_mean", ok ? json(r.point_distance_mean) : json(nullptr)},
        {"distance_to_nearest_colmap_median", ok ? json(r.point_distance_median) : json(nullptr)},
        {"distance_median_pct_radius", ok ? pct(r.point_distance_median) : json(nullptr)}}},
      {"per_camera", per_camera}};
}

int generateColmapReference(ColmapRunner& colmap, const fs::path& image_dir, const std::vector<std::string>& image_names,
                            const fs::path& out_dir) {
  fs::create_directories(out_dir);
  std::string list;
  for (const std::string& name : image_names) list += name + "\n";
  writeFileAtomic(out_dir / "image_list.txt", list);
  const fs::path db = out_dir / "database.db";
  fs::remove(db);
  const fs::path sparse = out_dir / "sparse";
  fs::remove_all(sparse);
  fs::create_directories(sparse);
  colmap.run("feature_extractor", {{"--database_path", db.string()},
                                   {"--image_path", image_dir.string()},
                                   {"--image_list_path", (out_dir / "image_list.txt").string()},
                                   {"--ImageReader.single_camera", "1"},
                                   {"--ImageReader.camera_model", "RADIAL"}});
  colmap.run("exhaustive_matcher", {{"--database_path", db.string()}});
  colmap.run("mapper", {{"--database_path", db.string()}, {"--image_path", image_dir.string()}, {"--output_path", sparse.string()}});

  // El mapper puede dejar varios modelos (0, 1, ...): se queda el que tenga más imágenes.
  fs::path best_dir;
  std::size_t best_images = 0;
  for (const auto& entry : fs::directory_iterator(sparse)) {
    if (!entry.is_directory()) continue;
    const fs::path txt = out_dir / ("txt_" + entry.path().filename().string());
    colmap.run("model_converter", {{"--input_path", entry.path().string()}, {"--output_path", txt.string()}, {"--output_type", "TXT"}});
    const std::size_t n = readColmapText(txt).images.size();
    if (n > best_images) best_images = n, best_dir = txt;
  }
  if (best_dir.empty()) throw ColmapError("el mapper de COLMAP no produjo ningún modelo en " + sparse.string());
  const fs::path final_dir = out_dir / "sparse_txt";
  fs::remove_all(final_dir);
  fs::rename(best_dir, final_dir);
  for (const auto& entry : fs::directory_iterator(out_dir))
    if (entry.is_directory() && entry.path().filename().string().rfind("txt_", 0) == 0) fs::remove_all(entry.path());
  return static_cast<int>(best_images);
}
