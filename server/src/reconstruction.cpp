#include "reconstruction.h"

#include "registration.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <queue>
#include <set>

namespace {

void say(const std::function<void(const std::string&)>& log, const std::string& text) {
  if (log) log(text);
}

// Fotos conectadas con `start` en el grafo de pares aceptados.
std::set<int> componentOf(int start, const std::vector<PairCandidate>& pairs) {
  std::map<int, std::vector<int>> adj;
  for (const PairCandidate& p : pairs) {
    adj[p.i].push_back(p.j);
    adj[p.j].push_back(p.i);
  }
  std::set<int> seen = {start};
  std::queue<int> q;
  q.push(start);
  while (!q.empty()) {
    const int n = q.front();
    q.pop();
    for (int m : adj[n])
      if (seen.insert(m).second) q.push(m);
  }
  return seen;
}

BundleOptions bundleOptions(const ReconstructionParams& params, const InitialPair& init) {
  BundleOptions o;
  o.max_iterations = params.ba_max_iterations;
  o.huber_delta_px = params.huber_delta_px;
  o.refine_focal = params.refine_focal;
  o.refine_distortion = params.refine_distortion;
  o.fixed_camera = init.i;  // calibre: la primera cámara del par inicial queda fija
  o.scale_camera = init.j;  // y la segunda fija la escala (|t| = 1 al empezar)
  return o;
}

void runBundle(SfmResult& res, const ReconstructionParams& params, const std::string& stage,
               const std::function<void(const std::string&)>& log) {
  BundleReport r = bundleAdjust(res.rec, bundleOptions(params, res.init));
  r.stage = stage;
  // Retriangulación (solo en la fase incremental): las pistas que fallaron con las poses
  // e intrínsecos anteriores (p. ej. por la K aproximada del par inicial) se reintentan
  // con los refinados. En las etapas finales no, para no reponer lo que filtra HU-17.
  const int retriangulated = stage == "incremental" ? triangulateTracks(res.rec, params) : 0;
  r.retriangulated = retriangulated;
  char text[220];
  std::snprintf(text, sizeof(text), "BA %s: RMS %.3f -> %.3f px, %d iteraciones, %.0f ms; +%d puntos retriangulados",
                stage.c_str(), r.rms_before_px, r.rms_after_px, r.iterations, r.time_ms, retriangulated);
  say(log, text);
  res.ba_runs.push_back(r);
}

// Escala monocular: se fija la distancia entre los centros del par inicial en 1 (el BA
// solo fija una componente de la traslación, así que se renormaliza al final).
void normalizeScale(Reconstruction& rec, int i, int j) {
  const double d = cv::norm(rec.cameras.at(j).pose.center() - rec.cameras.at(i).pose.center());
  if (!(d > 0)) return;
  const double s = 1.0 / d;
  for (auto& [img, cam] : rec.cameras)
    if (cam.registered) cam.pose.t *= s;
  for (Point3D& p : rec.points) p.xyz *= s;
}

// Corrección posterior al Sprint 3 (sesión llamav2): si con la K inicial (focal del EXIF)
// el par inicial no deja ningún punto dentro de los controles de HU-14, se prueba una
// rejilla de factores de focal SOLO para ese par. Se elige el factor con más puntos
// triangulados (desempate: menor error mediano). Los umbrales no cambian; después el
// bundle adjustment refina f como siempre. Devuelve los puntos triangulados.
int searchInitialFocal(SfmResult& res, const SfmInput& input, const ReconstructionParams& params,
                       const std::function<void(const std::string&)>& log) {
  const PairCandidate* pair = nullptr;
  for (const PairCandidate& p : input.pairs)
    if (p.i == res.init.i && p.j == res.init.j) pair = &p;
  if (!pair) return 0;
  const CameraModel& c0 = input.camera;
  const cv::Matx33d K0(c0.f, 0, c0.cx, 0, c0.f, c0.cy, 0, 0, 1);
  const cv::Matx33d F = K0.inv().t() * pair->E * K0.inv();  // E del Sprint 2 se calculó con K0

  Reconstruction best;
  RelativePose best_rel;
  int best_points = 0;
  double best_error = 0, best_factor = 1.0;
  for (int k = 0;; ++k) {
    const double factor = params.init_focal_factor_min + k * params.init_focal_factor_step;
    if (factor > params.init_focal_factor_max + 1e-9) break;
    CameraModel c = c0;
    c.f = c0.f * factor;
    const cv::Matx33d K(c.f, 0, c.cx, 0, c.f, c.cy, 0, 0, 1);
    PairCandidate p = *pair;
    p.E = K.t() * F * K;
    const RelativePose rel = recoverRelativePose(c, p);
    FocalTrial trial;
    trial.factor = factor;
    Reconstruction trial_rec = res.rec;
    if (rel.ok) {
      trial_rec.camera = c;
      trial_rec.cameras[res.init.i].pose = Pose{};
      trial_rec.cameras[res.init.j].pose = rel.pose_j;
      trial_rec.points.clear();
      trial_rec.reindexTracks();
      trial.points = triangulateTracks(trial_rec, params);
      std::vector<double> errors;
      for (const Point3D& pt : trial_rec.points)
        for (const Observation& o : pt.observations)
          errors.push_back(reprojectionError(c, trial_rec.cameras.at(o.image).pose, pt.xyz, o.xy));
      if (!errors.empty()) {
        std::nth_element(errors.begin(), errors.begin() + errors.size() / 2, errors.end());
        trial.median_error_px = errors[errors.size() / 2];
      }
    }
    res.focal_trials.push_back(trial);
    if (trial.points > best_points || (trial.points == best_points && trial.points > 0 && trial.median_error_px < best_error)) {
      best_points = trial.points;
      best_error = trial.median_error_px;
      best_factor = factor;
      best = std::move(trial_rec);
      best_rel = rel;
    }
  }
  char text[200];
  std::snprintf(text, sizeof(text),
                "búsqueda de focal del par inicial: %zu factores probados; elegido %.2f (f %.1f px), %d puntos, error mediano %.3f px",
                res.focal_trials.size(), best_factor, c0.f * best_factor, best_points, best_error);
  say(log, best_points > 0 ? std::string(text) : "búsqueda de focal del par inicial: ningún factor triangula puntos");
  if (best_points == 0) return 0;
  res.rec = std::move(best);
  res.init.relative = best_rel;
  res.init_focal_factor = best_factor;
  return best_points;
}

}  // namespace

SfmResult runIncrementalSfm(const SfmInput& input, const ReconstructionParams& params,
                            const std::function<void(const std::string&)>& log) {
  SfmResult res;
  Reconstruction& rec = res.rec;
  rec.camera = input.camera;
  rec.tracks = input.tracks;
  rec.reindexTracks();
  for (int img : input.images) rec.cameras[img].index = img;

  // 1. Par inicial (HU-13).
  res.init = chooseInitialPair(rec.camera, input.pairs, params);
  if (!res.init.found) {
    for (auto& [img, cam] : rec.cameras) cam.reason = "sin_par_inicial";
    say(log, "no hay par inicial válido");
    return res;
  }
  for (int img : {res.init.i, res.init.j}) {
    Camera& cam = rec.cameras[img];
    cam.index = img;
    cam.registered = true;
    cam.pose = img == res.init.i ? Pose{} : res.init.relative.pose_j;
    rec.registration_order.push_back(img);
  }

  // 2. Triangulación inicial (HU-14).
  int initial_points = triangulateTracks(rec, params);
  if (initial_points == 0 && params.init_focal_search) {
    say(log, "par inicial " + std::to_string(res.init.i) + "-" + std::to_string(res.init.j) +
                 ": 0 puntos con la focal inicial");
    initial_points = searchInitialFocal(res, input, params, log);
  }
  res.init_points = initial_points;
  say(log, "par inicial " + std::to_string(res.init.i) + "-" + std::to_string(res.init.j) + ": " +
               std::to_string(initial_points) + " puntos");
  runBundle(res, params, "incremental", log);

  // 3. Registro incremental (HU-15).
  const std::set<int> component = componentOf(res.init.i, input.pairs);
  std::map<int, int> failed_at;  // foto -> correspondencias cuando falló PnP
  int since_bundle = 0;
  while (true) {
    int best = -1, best_n = 0;
    for (const auto& [img, cam] : rec.cameras) {
      if (cam.registered || !component.count(img)) continue;
      const int n = static_cast<int>(correspondences2D3D(rec, img).points.size());
      const auto f = failed_at.find(img);
      if (f != failed_at.end() && n <= f->second) continue;  // solo se reintenta si ganó correspondencias
      if (n > best_n) best = img, best_n = n;
    }
    if (best < 0 || best_n < params.pnp_min_inliers) {
      // Fin del registro: se deja constancia de las correspondencias 2D-3D que quedaban.
      std::string left;
      for (const auto& [img, cam] : rec.cameras)
        if (!cam.registered && component.count(img)) {
          const int n = static_cast<int>(correspondences2D3D(rec, img).points.size());
          if (n > 0) left += " " + std::to_string(img) + ":" + std::to_string(n);
        }
      say(log, "fin del registro; correspondencias 2D-3D de las cámaras restantes (mínimo " +
                   std::to_string(params.pnp_min_inliers) + "):" + (left.empty() ? " ninguna" : left));
      break;
    }

    const RegistrationResult reg = registerCamera(rec, best, params);
    if (!reg.ok) {
      failed_at[best] = best_n;
      rec.cameras[best].reason = reg.reason;
      say(log, "foto " + std::to_string(best) + ": no registrada (" + reg.reason + ", " +
                   std::to_string(reg.n_inliers) + "/" + std::to_string(reg.n_correspondences) + ")");
      continue;
    }
    const int added = triangulateTracks(rec, params, best);
    say(log, "foto " + std::to_string(best) + ": registrada, " + std::to_string(reg.n_inliers) + "/" +
                 std::to_string(reg.n_correspondences) + " inliers PnP, +" + std::to_string(added) + " puntos");
    if (++since_bundle >= params.ba_every) {
      runBundle(res, params, "incremental", log);
      since_bundle = 0;
    }
  }

  // 4. Bundle adjustment final (HU-16), filtrado (HU-17), segundo BA y filtrado final.
  res.errors_before = computeCameraErrors(rec);
  runBundle(res, params, "final", log);
  res.filter_first = filterPoints(rec, params.min_views, params.max_reproj_error_px);
  say(log, "filtrado: " + std::to_string(res.filter_first.points_before) + " -> " +
               std::to_string(res.filter_first.points_after) + " puntos");
  runBundle(res, params, "final_2", log);
  res.filter_final = filterPoints(rec, params.min_views, params.max_reproj_error_px);
  normalizeScale(rec, res.init.i, res.init.j);
  res.errors_after = computeCameraErrors(rec);
  say(log, "filtrado final: " + std::to_string(res.filter_final.points_before) + " -> " +
               std::to_string(res.filter_final.points_after) + " puntos");

  for (auto& [img, cam] : rec.cameras) {
    if (cam.registered) continue;
    if (!component.count(img))
      cam.reason = "componente_desconectado";
    else if (cam.reason.empty())
      cam.reason = "pocas_correspondencias_2d3d";
  }
  return res;
}
