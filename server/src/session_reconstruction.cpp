#include "session_reconstruction.h"

#include "atomic_file.h"
#include "manifest.h"
#include "matching_report.h"
#include "reconstruction_io.h"
#include "session_store.h"
#include "tracks.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void say(const SessionReconstructionOptions& o, const std::string& text) {
  if (o.log) o.log(text);
}

// Color promedio (R, G, B) de las observaciones de cada punto en sus fotos.
void assignColors(Reconstruction& rec, const fs::path& session_dir, const std::map<int, std::string>& files) {
  std::map<int, std::vector<std::pair<std::size_t, cv::Point2d>>> by_image;
  for (std::size_t k = 0; k < rec.points.size(); ++k)
    for (const Observation& o : rec.points[k].observations) by_image[o.image].push_back({k, o.xy});
  std::vector<cv::Vec3d> sum(rec.points.size(), cv::Vec3d(0, 0, 0));
  std::vector<int> count(rec.points.size(), 0);
  for (const auto& [img, list] : by_image) {
    const auto f = files.find(img);
    if (f == files.end()) continue;
    const cv::Mat bgr = cv::imread((session_dir / f->second).string(), cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
    if (bgr.empty()) continue;
    for (const auto& [k, xy] : list) {
      const int x = static_cast<int>(std::lround(xy.x)), y = static_cast<int>(std::lround(xy.y));
      if (x < 0 || y < 0 || x >= bgr.cols || y >= bgr.rows) continue;
      const cv::Vec3b c = bgr.at<cv::Vec3b>(y, x);
      sum[k] += cv::Vec3d(c[2], c[1], c[0]);
      ++count[k];
    }
  }
  for (std::size_t k = 0; k < rec.points.size(); ++k)
    if (count[k] > 0) {
      const cv::Vec3d m = sum[k] * (1.0 / count[k]);
      rec.points[k].rgb = cv::Vec3b(cv::saturate_cast<uchar>(m[0]), cv::saturate_cast<uchar>(m[1]),
                                    cv::saturate_cast<uchar>(m[2]));
    }
}

// Vista ortográfica de la nube y de las cámaras sobre los ejes (u, v) dados.
void drawView(const fs::path& file, const Reconstruction& rec, const cv::Vec3d& origin, const cv::Vec3d& u_axis,
              const cv::Vec3d& v_axis, const std::string& title) {
  constexpr int kSize = 1200, kMargin = 60;
  std::vector<double> us, vs;
  for (const Point3D& p : rec.points) {
    us.push_back((p.xyz - origin).dot(u_axis));
    vs.push_back((p.xyz - origin).dot(v_axis));
  }
  std::vector<cv::Vec3d> centers;
  std::vector<int> labels;
  for (const auto& [img, cam] : rec.cameras)
    if (cam.registered) {
      centers.push_back(cam.pose.center());
      labels.push_back(img);
    }
  // Límites: percentiles 2-98 de los puntos (descarta atípicos) y todas las cámaras.
  auto range = [](std::vector<double> v, double& lo, double& hi) {
    if (v.empty()) return;
    std::sort(v.begin(), v.end());
    lo = std::min(lo, v[v.size() * 2 / 100]);
    hi = std::max(hi, v[std::min(v.size() - 1, v.size() * 98 / 100)]);
  };
  double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
  range(us, u0, u1);
  range(vs, v0, v1);
  for (const cv::Vec3d& C : centers) {
    u0 = std::min(u0, (C - origin).dot(u_axis));
    u1 = std::max(u1, (C - origin).dot(u_axis));
    v0 = std::min(v0, (C - origin).dot(v_axis));
    v1 = std::max(v1, (C - origin).dot(v_axis));
  }
  if (!(u1 > u0) || !(v1 > v0)) return;
  const double scale = (kSize - 2 * kMargin) / std::max(u1 - u0, v1 - v0);
  auto toPixel = [&](const cv::Vec3d& X) {
    const double u = (X - origin).dot(u_axis), v = (X - origin).dot(v_axis);
    return cv::Point(static_cast<int>(kMargin + (u - u0) * scale), static_cast<int>(kMargin + (v - v0) * scale));
  };
  cv::Mat canvas(kSize, kSize, CV_8UC3, cv::Scalar(255, 255, 255));
  for (const Point3D& p : rec.points) cv::circle(canvas, toPixel(p.xyz), 2, cv::Scalar(p.rgb[2], p.rgb[1], p.rgb[0]), -1);
  std::size_t k = 0;
  for (const auto& [img, cam] : rec.cameras) {
    if (!cam.registered) continue;
    const cv::Vec3d C = cam.pose.center();
    const cv::Vec3d dir = cam.pose.R().t() * cv::Vec3d(0, 0, 1);  // eje óptico en el mundo
    const double len = 0.06 * std::max(u1 - u0, v1 - v0);
    cv::line(canvas, toPixel(C), toPixel(C + dir * len), cv::Scalar(0, 0, 200), 2);
    cv::circle(canvas, toPixel(C), 7, cv::Scalar(0, 0, 220), -1);
    cv::putText(canvas, std::to_string(labels[k++]), toPixel(C) + cv::Point(8, -8), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                cv::Scalar(0, 0, 0), 1);
  }
  cv::putText(canvas, title, {20, 35}, cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(0, 0, 0), 2);
  cv::imwrite(file.string(), canvas, {cv::IMWRITE_JPEG_QUALITY, 92});
}

void writeDebugViews(const fs::path& dir, const Reconstruction& rec) {
  std::vector<cv::Vec3d> centers;
  for (const auto& [img, cam] : rec.cameras)
    if (cam.registered) centers.push_back(cam.pose.center());
  if (centers.empty()) return;
  cv::Vec3d c(0, 0, 0);
  for (const cv::Vec3d& x : centers) c += x;
  c *= 1.0 / static_cast<double>(centers.size());
  // Ejes del anillo por PCA de los centros: los dos primeros en el plano, el tercero normal.
  cv::Vec3d e0(1, 0, 0), e1(0, 1, 0), e2(0, 0, 1);
  if (centers.size() >= 3) {
    cv::Mat data(static_cast<int>(centers.size()), 3, CV_64F);
    for (int r = 0; r < data.rows; ++r)
      for (int k = 0; k < 3; ++k) data.at<double>(r, k) = centers[static_cast<std::size_t>(r)][k] - c[k];
    cv::Mat cov = data.t() * data, evals, evecs;
    cv::eigen(cov, evals, evecs);
    e0 = cv::Vec3d(evecs.at<double>(0, 0), evecs.at<double>(0, 1), evecs.at<double>(0, 2));
    e1 = cv::Vec3d(evecs.at<double>(1, 0), evecs.at<double>(1, 1), evecs.at<double>(1, 2));
    e2 = e0.cross(e1);
  }
  drawView(dir / "debug_top.jpg", rec, c, e0, e1, "Vista superior (plano del anillo de camaras)");
  drawView(dir / "debug_side.jpg", rec, c, e0, -e2, "Vista lateral (normal al anillo hacia arriba)");
}

}  // namespace

SessionReconstructionResult runSessionReconstruction(const fs::path& session_dir, ReconstructionParams params,
                                                     const SessionReconstructionOptions& options) {
  const Clock::time_point t_start = Clock::now();
  if (!fs::is_directory(session_dir)) throw std::runtime_error("no existe la sesión " + session_dir.string());
  if (!fs::exists(session_dir / "manifest.json"))
    throw std::runtime_error("la sesión no tiene manifest.json: " + session_dir.string());
  if (!fs::exists(session_dir / "matching_report.json"))
    throw std::runtime_error("falta matching_report.json: ejecuta antes scanner_match sobre " + session_dir.string());
  if (options.include_rejected) params.exclude_rejected_photos = false;

  SessionReconstructionResult out;
  ReconstructionReport& report = out.report;
  report.params = params;
  report.solver = bundleSolverName();
  report.generated_at = formatLocalTimestamp(std::chrono::system_clock::now());

  // 1. Carga.
  Clock::time_point t = Clock::now();
  const Manifest manifest = readManifest(session_dir / "manifest.json");
  const MatchingReport matching = readMatchingReport(session_dir / "matching_report.json");
  report.session_id = manifest.session_id;
  std::map<int, bool> accepted;
  std::map<int, Quat> imu;
  std::map<int, std::string> files;
  for (const PhotoRecord& p : manifest.photos) {
    accepted[p.metadata.index] = p.quality.accepted;
    files[p.metadata.index] = p.file;
    if (p.metadata.orientation_q) imu[p.metadata.index] = *p.metadata.orientation_q;
  }

  SfmInput input;
  std::set<int> candidates;
  for (const ViewReport& v : matching.views) {
    if (params.exclude_rejected_photos && !accepted[v.index]) {
      report.excluded_photos.push_back({v.index, "rechazada_calidad"});
      continue;
    }
    if (candidates.empty()) {
      input.camera.f = v.intrinsics.focal_px;
      input.camera.cx = v.intrinsics.cx;
      input.camera.cy = v.intrinsics.cy;
      input.camera.width = v.intrinsics.width;
      input.camera.height = v.intrinsics.height;
    } else if (v.intrinsics.width != input.camera.width || v.intrinsics.height != input.camera.height ||
               std::abs(v.intrinsics.focal_px - input.camera.f) > 1e-6) {
      report.warnings.push_back("la foto " + std::to_string(v.index) +
                                " tiene otra K inicial; se usa una sola K compartida (la de la primera foto)");
    }
    candidates.insert(v.index);
  }
  for (const ExcludedView& e : matching.excluded_views) {
    if (e.reason == "rechazada_calidad" && !params.exclude_rejected_photos) {
      candidates.insert(e.index);  // incluida a pedido, pero scanner_match no la emparejó
      report.warnings.push_back("la foto " + std::to_string(e.index) +
                                " se incluyó (--include-rejected) pero no tiene pares en matching_report.json");
    } else {
      report.excluded_photos.push_back({e.index, e.reason});
    }
  }
  std::sort(report.excluded_photos.begin(), report.excluded_photos.end());
  input.images.assign(candidates.begin(), candidates.end());
  report.initial_intrinsics = input.camera;

  std::vector<PairMatches> pair_matches;
  for (const PairReport& p : matching.pairs) {
    if (!p.accepted || p.inliers_file.empty() || !p.E || !candidates.count(p.i) || !candidates.count(p.j)) continue;
    PairMatches pm{p.i, p.j, readInliers(session_dir / p.inliers_file)};
    PairCandidate pc;
    pc.i = p.i;
    pc.j = p.j;
    pc.E = *p.E;
    for (const InlierCorrespondence& c : pm.inliers) {
      pc.pts_i.emplace_back(c.point_i.x, c.point_i.y);
      pc.pts_j.emplace_back(c.point_j.x, c.point_j.y);
    }
    input.pairs.push_back(std::move(pc));
    pair_matches.push_back(std::move(pm));
  }
  report.timing_ms["load"] = msSince(t);
  say(options, std::to_string(input.images.size()) + " fotos candidatas, " + std::to_string(input.pairs.size()) +
                   " pares aceptados");

  // 2. Pistas.
  t = Clock::now();
  TrackStats track_stats;
  input.tracks = buildTracks(pair_matches, &track_stats);
  report.tracks = track_stats.tracks;
  report.tracks_discarded_inconsistent = track_stats.discarded_inconsistent;
  report.timing_ms["tracks"] = msSince(t);
  say(options, std::to_string(track_stats.tracks) + " pistas (" + std::to_string(track_stats.discarded_inconsistent) +
                   " descartadas por inconsistentes)");

  // 3. Reconstrucción (HU-13 a HU-18).
  t = Clock::now();
  out.sfm = runIncrementalSfm(input, params, options.log);
  report.timing_ms["reconstruction"] = msSince(t);
  double ba_ms = 0;
  for (const BundleReport& b : out.sfm.ba_runs) ba_ms += b.time_ms;
  report.timing_ms["bundle_adjustment"] = ba_ms;
  Reconstruction& rec = out.sfm.rec;

  // 4. Color de los puntos.
  t = Clock::now();
  assignColors(rec, session_dir, files);
  report.timing_ms["colors"] = msSince(t);

  // 5. Reporte (HU-18).
  report.final_intrinsics = rec.camera;
  report.init_i = out.sfm.init.i;
  report.init_j = out.sfm.init.j;
  report.init_evaluated = out.sfm.init.evaluated;
  report.init_points = out.sfm.init_points;
  report.init_focal_factor = out.sfm.init_focal_factor;
  for (const FocalTrial& trial : out.sfm.focal_trials)
    report.focal_search.push_back(
        json{{"factor", trial.factor}, {"points", trial.points}, {"median_error_px", trial.median_error_px}});
  if (!out.sfm.focal_trials.empty())
    report.warnings.push_back("la focal inicial no triangulaba ningún punto del par inicial; se usó el factor " +
                              json(out.sfm.init_focal_factor).dump() + " de la rejilla");
  for (const auto& [img, cam] : rec.cameras) {
    CameraSummary c;
    c.index = img;
    c.registered = cam.registered;
    c.reason = cam.reason;
    if (cam.registered) {
      const auto b = out.sfm.errors_before.per_camera.find(img);
      if (b != out.sfm.errors_before.per_camera.end()) c.before = b->second;
      const auto a = out.sfm.errors_after.per_camera.find(img);
      if (a != out.sfm.errors_after.per_camera.end()) c.after = a->second;
    }
    report.cameras.push_back(c);
  }
  report.global_before = out.sfm.errors_before.global;
  report.global_after = out.sfm.errors_after.global;
  report.filter_first = out.sfm.filter_first;
  report.filter_final = out.sfm.filter_final;
  report.points_before_filter = out.sfm.filter_first.points_before;
  report.points_after_filter = static_cast<int>(rec.points.size());
  std::vector<double> lengths;
  for (const Point3D& p : rec.points) lengths.push_back(static_cast<double>(p.observations.size()));
  report.track_length = describe(lengths);
  report.ba_runs = out.sfm.ba_runs;
  report.imu_consistency = imuConsistencyJson(rec, imu);
  report.ring_geometry = ringGeometryJson(rec);
  if (!out.sfm.init.found) report.warnings.push_back("no se encontró un par inicial válido");

  // 6. Salidas.
  t = Clock::now();
  const fs::path dir = session_dir / "reconstruction";
  fs::create_directories(dir);
  ReconstructionFile file;
  file.session_id = report.session_id;
  file.generated_at = report.generated_at;
  file.params = params;
  file.rec = rec;
  writeReconstruction(dir / "reconstruction.json", file);
  writePly(dir / "sparse_cloud.ply", rec);
  if (options.debug) writeDebugViews(dir, rec);
  report.timing_ms["outputs"] = msSince(t);
  report.timing_ms["total"] = msSince(t_start);
  writeFileAtomic(dir / "reconstruction_report.json", toJson(report).dump(2) + '\n');
  return out;
}
