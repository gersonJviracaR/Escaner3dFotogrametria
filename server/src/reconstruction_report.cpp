#include "reconstruction_report.h"

#include <algorithm>
#include <cmath>
#include <numeric>

using nlohmann::json;

namespace {

struct Accumulator {
  int n = 0;
  double sum = 0, sum2 = 0, max = 0;
  void add(double e) {
    ++n;
    sum += e;
    sum2 += e * e;
    max = std::max(max, e);
  }
  ErrorStats stats() const {
    if (n == 0) return {};
    return {n, sum / n, std::sqrt(sum2 / n), max};
  }
};

json intrinsicsJson(const CameraModel& c) {
  return json{{"f", c.f},   {"k1", c.k1},          {"k2", c.k2},           {"cx", c.cx},
              {"cy", c.cy}, {"width", c.width},    {"height", c.height}};
}

json statsJson(const Stats& s) {
  return json{{"min", s.min}, {"median", s.median}, {"mean", s.mean}, {"max", s.max}};
}

json filterJson(const FilterStats& f) {
  return json{{"points_before", f.points_before},
              {"points_after", f.points_after},
              {"observations_not_visible", f.observations_not_visible},
              {"observations_high_error", f.observations_high_error},
              {"points_few_views", f.points_few_views}};
}

json bundleJson(const BundleReport& b) {
  return json{{"stage", b.stage},
              {"solver", b.solver},
              {"iterations", b.iterations},
              {"initial_cost", b.initial_cost},
              {"final_cost", b.final_cost},
              {"rms_before_px", b.rms_before_px},
              {"rms_after_px", b.rms_after_px},
              {"cameras", b.n_cameras},
              {"points", b.n_points},
              {"observations", b.n_observations},
              {"termination", b.termination},
              {"time_ms", b.time_ms}};
}

}  // namespace

CameraErrors computeCameraErrors(const Reconstruction& rec) {
  std::map<int, Accumulator> per;
  Accumulator global;
  for (const auto& [img, cam] : rec.cameras)
    if (cam.registered) per[img];
  for (const Point3D& p : rec.points)
    for (const Observation& o : p.observations) {
      const auto it = rec.cameras.find(o.image);
      if (it == rec.cameras.end() || !it->second.registered) continue;
      const double e = reprojectionError(rec.camera, it->second.pose, p.xyz, o.xy);
      per[o.image].add(e);
      global.add(e);
    }
  CameraErrors out;
  for (const auto& [img, acc] : per) out.per_camera[img] = acc.stats();
  out.global = global.stats();
  return out;
}

Stats describe(std::vector<double> v) {
  if (v.empty()) return {};
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  Stats s;
  s.min = v.front();
  s.max = v.back();
  s.median = n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
  s.mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(n);
  return s;
}

json toJson(const ErrorStats& e) {
  return json{{"n", e.n}, {"mean_px", e.mean}, {"rms_px", e.rms}, {"max_px", e.max}};
}

json toJson(const ReconstructionReport& r) {
  json cameras = json::array();
  int registered = 0;
  for (const CameraSummary& c : r.cameras) {
    registered += c.registered ? 1 : 0;
    cameras.push_back(json{{"index", c.index},
                           {"registered", c.registered},
                           {"reason", c.registered ? json(nullptr) : json(c.reason)},
                           {"before", c.registered ? toJson(c.before) : json(nullptr)},
                           {"after", c.registered ? toJson(c.after) : json(nullptr)}});
  }
  json evaluated = json::array();
  for (const InitialPairChoice& e : r.init_evaluated)
    evaluated.push_back(json{{"i", e.i},
                             {"j", e.j},
                             {"n_inliers", e.n_inliers},
                             {"front_ratio", e.front_ratio},
                             {"median_angle_deg", e.median_angle_deg},
                             {"result", e.reason}});
  json excluded = json::array();
  for (const auto& [index, reason] : r.excluded_photos) excluded.push_back(json{{"index", index}, {"reason", reason}});
  json ba = json::array();
  for (const BundleReport& b : r.ba_runs) ba.push_back(bundleJson(b));

  return json{
      {"schema_version", 1},
      {"session_id", r.session_id},
      {"generated_at", r.generated_at},
      {"params", toJson(r.params)},
      {"solver", r.solver},
      {"intrinsics", {{"initial", intrinsicsJson(r.initial_intrinsics)}, {"final", intrinsicsJson(r.final_intrinsics)}}},
      {"initial_pair", {{"i", r.init_i}, {"j", r.init_j}, {"evaluated", evaluated}}},
      {"excluded_photos", excluded},
      {"cameras", cameras},
      {"global", {{"before", toJson(r.global_before)}, {"after", toJson(r.global_after)}}},
      {"points",
       {{"tracks", r.tracks},
        {"tracks_discarded_inconsistent", r.tracks_discarded_inconsistent},
        {"before_filter", r.points_before_filter},
        {"after_filter", r.points_after_filter},
        {"filter_first", filterJson(r.filter_first)},
        {"filter_final", filterJson(r.filter_final)},
        {"track_length", statsJson(r.track_length)}}},
      {"bundle_adjustment", ba},
      {"imu_consistency", r.imu_consistency},
      {"ring_geometry", r.ring_geometry},
      {"timing_ms", r.timing_ms},
      {"warnings", r.warnings},
      {"summary",
       {{"cameras_candidates", static_cast<int>(r.cameras.size())},
        {"cameras_registered", registered},
        {"cameras_unregistered", static_cast<int>(r.cameras.size()) - registered},
        {"points", r.points_after_filter},
        {"rms_before_px", r.global_before.rms},
        {"rms_after_px", r.global_after.rms}}}};
}
