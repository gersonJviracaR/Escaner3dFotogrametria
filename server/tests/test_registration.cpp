// Pruebas de HU-15: registro incremental con PnP-RANSAC y triangulación de puntos nuevos.
#include "registration.h"
#include "test_sfm_scene.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

ReconstructionParams testParams() {
  ReconstructionParams p;
  p.ba_every = 1000000;  // HU-15 se prueba sin bundle adjustment
  return p;
}

// Alinea los centros estimados con los verdaderos y devuelve el peor error de centro
// (relativo al radio del anillo) y de rotación (grados).
std::pair<double, double> poseErrors(const scene::SfmScene& s, const Reconstruction& rec) {
  std::vector<cv::Vec3d> est, truth;
  for (const auto& [index, cam] : rec.cameras)
    if (cam.registered) {
      est.push_back(cam.pose.center());
      truth.push_back(s.poses.at(index).center());
    }
  const scene::Similarity sim = scene::umeyama(est, truth);
  double worst_c = 0, worst_r = 0;
  for (const auto& [index, cam] : rec.cameras) {
    if (!cam.registered) continue;
    worst_c = std::max(worst_c, cv::norm(sim.apply(cam.pose.center()) - s.poses.at(index).center()) / 6.0);
    // R_verdadera ≈ R_estimada · Rsᵀ
    const cv::Matx33d R_aligned = cam.pose.R() * sim.R.t();
    worst_r = std::max(worst_r, scene::rotationAngleDeg(R_aligned * s.poses.at(index).R().t()));
  }
  return {worst_c, worst_r};
}

}  // namespace

TEST(Registration, RingOf12CamerasIsFullyRegistered) {
  const scene::SfmScene s = scene::makeRingScene();
  const SfmResult r = runIncrementalSfm(s.input, testParams());
  ASSERT_TRUE(r.init.found);
  EXPECT_EQ(r.rec.registeredCount(), 12);
  EXPECT_EQ(r.rec.registration_order.size(), 12u);
  const auto [center_err, rot_err] = poseErrors(s, r.rec);
  EXPECT_LT(center_err, 0.02) << "error de centro relativo al radio";
  EXPECT_LT(rot_err, 1.0) << "grados";
}

TEST(Registration, NewPointsAreTriangulatedAsCamerasAreAdded) {
  const scene::SfmScene s = scene::makeRingScene();
  Reconstruction rec;
  rec.camera = s.camera;
  rec.tracks = s.input.tracks;
  // La mitad de los puntos no se ven desde la foto 1: solo se podrán triangular
  // cuando se registre otra cámara que los vea.
  for (std::size_t t = 0; t < rec.tracks.size(); t += 2) {
    auto& obs = rec.tracks[t].observations;
    obs.erase(std::remove_if(obs.begin(), obs.end(), [](const Observation& o) { return o.image == 1; }), obs.end());
  }
  rec.reindexTracks();
  for (int img : s.input.images) rec.cameras[img].index = img;
  // Dos cámaras registradas con su pose verdadera.
  for (int img : {1, 2}) {
    rec.cameras[img].registered = true;
    rec.cameras[img].pose = s.poses.at(img);
  }
  const int initial = triangulateTracks(rec, testParams());
  EXPECT_GT(initial, 300);

  const RegistrationResult reg = registerCamera(rec, 3, testParams());
  ASSERT_TRUE(reg.ok) << reg.reason;
  EXPECT_GE(reg.n_inliers, 20);
  EXPECT_LT(scene::rotationAngleDeg(rec.cameras[3].pose.R() * s.poses.at(3).R().t()), 0.5);
  const int added = triangulateTracks(rec, testParams(), 3);
  EXPECT_GT(added, 0);  // pistas vistas en 2-3 (o 1-3) que aún no tenían punto
  for (const Point3D& p : rec.points) {
    EXPECT_GE(p.observations.size(), 2u);
    for (const Observation& o : p.observations) EXPECT_TRUE(rec.cameras[o.image].registered);
  }
}

TEST(Registration, CameraWithTooFewCorrespondencesStaysUnregistered) {
  scene::RingOptions o;
  o.weak_camera = true;
  const scene::SfmScene s = scene::makeRingScene(o);
  const SfmResult r = runIncrementalSfm(s.input, testParams());
  EXPECT_EQ(r.rec.registeredCount(), 12);
  const Camera& weak = r.rec.cameras.at(13);
  EXPECT_FALSE(weak.registered);
  EXPECT_EQ(weak.reason, "pocas_correspondencias_2d3d");
}

TEST(Registration, DisconnectedComponentIsReported) {
  scene::RingOptions o;
  o.separate_component = true;
  const scene::SfmScene s = scene::makeRingScene(o);
  const SfmResult r = runIncrementalSfm(s.input, testParams());
  EXPECT_EQ(r.rec.registeredCount(), 12);
  for (int img : {20, 21}) {
    EXPECT_FALSE(r.rec.cameras.at(img).registered);
    EXPECT_EQ(r.rec.cameras.at(img).reason, "componente_desconectado");
  }
}

TEST(Registration, PnPRejectsCameraWithoutEnoughInliers) {
  const scene::SfmScene s = scene::makeRingScene();
  Reconstruction rec;
  rec.camera = s.camera;
  rec.tracks = s.input.tracks;
  rec.reindexTracks();
  for (int img : s.input.images) rec.cameras[img].index = img;
  for (int img : {1, 2}) {
    rec.cameras[img].registered = true;
    rec.cameras[img].pose = s.poses.at(img);
  }
  triangulateTracks(rec, testParams());
  ReconstructionParams strict = testParams();
  strict.pnp_min_inliers = 100000;
  const RegistrationResult reg = registerCamera(rec, 3, strict);
  EXPECT_FALSE(reg.ok);
  EXPECT_FALSE(rec.cameras[3].registered);
  EXPECT_FALSE(reg.reason.empty());
}
