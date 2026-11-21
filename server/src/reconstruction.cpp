#include "reconstruction.h"

#include "registration.h"

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
  const int initial_points = triangulateTracks(rec, params);
  say(log, "par inicial " + std::to_string(res.init.i) + "-" + std::to_string(res.init.j) + ": " +
               std::to_string(initial_points) + " puntos");

  // 3. Registro incremental (HU-15).
  const std::set<int> component = componentOf(res.init.i, input.pairs);
  std::map<int, int> failed_at;  // foto -> correspondencias cuando falló PnP
  while (true) {
    int best = -1, best_n = 0;
    for (const auto& [img, cam] : rec.cameras) {
      if (cam.registered || !component.count(img)) continue;
      const int n = static_cast<int>(correspondences2D3D(rec, img).points.size());
      const auto f = failed_at.find(img);
      if (f != failed_at.end() && n <= f->second) continue;  // solo se reintenta si ganó correspondencias
      if (n > best_n) best = img, best_n = n;
    }
    if (best < 0 || best_n < params.pnp_min_inliers) break;

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
  }

  for (auto& [img, cam] : rec.cameras) {
    if (cam.registered) continue;
    if (!component.count(img))
      cam.reason = "componente_desconectado";
    else if (cam.reason.empty())
      cam.reason = "pocas_correspondencias_2d3d";
  }
  return res;
}
