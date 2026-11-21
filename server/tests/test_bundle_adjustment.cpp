// Pruebas de HU-16: bundle adjustment con Ceres.
#include "bundle_adjustment.h"
#include "test_sfm_scene.h"

#include <gtest/gtest.h>
#include <opencv2/calib3d.hpp>

#include <algorithm>
#include <cmath>

namespace {

// Reconstrucción con las poses y puntos verdaderos y las observaciones con ruido de la escena.
Reconstruction fromScene(const scene::SfmScene& s) {
  Reconstruction rec;
  rec.camera = s.camera;
  for (const auto& [img, pose] : s.poses) {
    Camera& c = rec.cameras[img];
    c.index = img;
    c.registered = true;
    c.pose = pose;
    rec.registration_order.push_back(img);
  }
  for (std::size_t k = 0; k < s.points.size(); ++k) {
    Point3D p;
    p.xyz = s.points[k];
    for (const auto& [img, obs] : s.observed) {
      const auto it = obs.find(static_cast<int>(k));
      if (it != obs.end()) p.observations.push_back({img, static_cast<int>(k), it->second});
    }
    if (p.observations.size() >= 2) rec.points.push_back(p);
  }
  return rec;
}

// Rotación pequeña alrededor de un eje aleatorio y desplazamiento.
Pose perturb(const Pose& p, double deg, const cv::Vec3d& dt, int seed) {
  cv::RNG rng(static_cast<std::uint64_t>(seed));
  cv::Vec3d axis(rng.uniform(-1.0, 1.0), rng.uniform(-1.0, 1.0), rng.uniform(-1.0, 1.0));
  axis = cv::normalize(axis) * (deg * scene::kPi / 180.0);
  cv::Mat dR;
  cv::Rodrigues(axis, dR);
  return Pose::fromRt(cv::Matx33d(dR) * p.R(), p.t + dt);
}

void perturbAll(Reconstruction& rec, int keep_fixed) {
  int seed = 1;
  for (auto& [img, cam] : rec.cameras)
    if (img != keep_fixed) cam.pose = perturb(cam.pose, 0.8, {0.03, -0.02, 0.04}, seed++);
  cv::RNG rng(5);
  for (Point3D& p : rec.points) p.xyz += cv::Vec3d(rng.gaussian(0.03), rng.gaussian(0.03), rng.gaussian(0.03));
}

struct PoseErr {
  double center = 0;
  double rotation = 0;
};

PoseErr poseError(const scene::SfmScene& s, const Reconstruction& rec) {
  std::vector<cv::Vec3d> est, truth;
  for (const auto& [img, cam] : rec.cameras) {
    est.push_back(cam.pose.center());
    truth.push_back(s.poses.at(img).center());
  }
  const scene::Similarity sim = scene::umeyama(est, truth);
  PoseErr e;
  for (const auto& [img, cam] : rec.cameras) {
    e.center = std::max(e.center, cv::norm(sim.apply(cam.pose.center()) - s.poses.at(img).center()));
    e.rotation =
        std::max(e.rotation, scene::rotationAngleDeg(cam.pose.R() * sim.R.t() * s.poses.at(img).R().t()));
  }
  return e;
}

}  // namespace

TEST(BundleAdjustment, ReducesRmsToNoiseLevelAndRecoversPoses) {
  const scene::SfmScene s = scene::makeRingScene();
  Reconstruction rec = fromScene(s);
  const double rms_truth = reprojectionRms(rec);  // del orden de 0,3 * raíz(2) px
  perturbAll(rec, 1);
  const BundleReport r = bundleAdjust(rec, BundleOptions{});
  EXPECT_GT(r.rms_before_px, 5.0);
  EXPECT_LT(r.rms_after_px, r.rms_before_px);
  EXPECT_LT(r.rms_after_px, rms_truth * 1.05);
  EXPECT_NEAR(r.rms_after_px, reprojectionRms(rec), 1e-9);
  EXPECT_LT(r.final_cost, r.initial_cost);
  EXPECT_GT(r.iterations, 0);
  EXPECT_FALSE(r.solver.empty());
  const PoseErr e = poseError(s, rec);
  EXPECT_LT(e.center, 0.02);
  EXPECT_LT(e.rotation, 0.1);
}

TEST(BundleAdjustment, QuaternionsStayUnitAndGaugeIsFixed) {
  const scene::SfmScene s = scene::makeRingScene();
  Reconstruction rec = fromScene(s);
  perturbAll(rec, 1);
  const Pose cam1 = rec.cameras.at(1).pose;
  const cv::Vec3d t2 = rec.cameras.at(2).pose.t;
  int largest = 0;
  for (int k = 1; k < 3; ++k)
    if (std::abs(t2[k]) > std::abs(t2[largest])) largest = k;
  bundleAdjust(rec, BundleOptions{});
  for (const auto& [img, cam] : rec.cameras) EXPECT_NEAR(quaternionNorm(cam.pose.q), 1.0, 1e-9) << img;
  for (int k = 0; k < 4; ++k) EXPECT_EQ(rec.cameras.at(1).pose.q[k], cam1.q[k]);
  for (int k = 0; k < 3; ++k) EXPECT_EQ(rec.cameras.at(1).pose.t[k], cam1.t[k]);
  EXPECT_EQ(rec.cameras.at(2).pose.t[largest], t2[largest]);  // fija la escala
}

TEST(BundleAdjustment, HuberLimitsTheEffectOfAnOutlier) {
  const scene::SfmScene s = scene::makeRingScene();
  Reconstruction rec = fromScene(s);
  perturbAll(rec, 1);
  rec.points[0].observations[0].xy.x += 60;  // observación atípica
  bundleAdjust(rec, BundleOptions{});
  const PoseErr e = poseError(s, rec);
  EXPECT_LT(e.center, 0.02);
  EXPECT_LT(e.rotation, 0.1);
  std::vector<double> errs;
  for (std::size_t k = 1; k < rec.points.size(); ++k)
    for (const Observation& o : rec.points[k].observations)
      errs.push_back(reprojectionError(rec.camera, rec.cameras.at(o.image).pose, rec.points[k].xyz, o.xy));
  std::nth_element(errs.begin(), errs.begin() + errs.size() / 2, errs.end());
  EXPECT_LT(errs[errs.size() / 2], 0.5);
}

TEST(BundleAdjustment, RecoversFocalFromTenPercentError) {
  for (double factor : {0.9, 1.1}) {
    const scene::SfmScene s = scene::makeRingScene();
    Reconstruction rec = fromScene(s);
    perturbAll(rec, 1);
    rec.camera.f = s.camera.f * factor;
    bundleAdjust(rec, BundleOptions{});
    EXPECT_NEAR(rec.camera.f / s.camera.f, 1.0, 0.02) << "factor inicial " << factor;
  }
}

TEST(BundleAdjustment, FocalIsConstantWhenNotRefined) {
  const scene::SfmScene s = scene::makeRingScene();
  Reconstruction rec = fromScene(s);
  perturbAll(rec, 1);
  rec.camera.f = s.camera.f * 1.1;
  BundleOptions o;
  o.refine_focal = false;
  o.refine_distortion = false;
  bundleAdjust(rec, o);
  EXPECT_EQ(rec.camera.f, s.camera.f * 1.1);
  EXPECT_EQ(rec.camera.k1, 0.0);
  EXPECT_EQ(rec.camera.k2, 0.0);
}

TEST(BundleAdjustment, IncrementalPipelineRunsBundleAdjustment) {
  const scene::SfmScene s = scene::makeRingScene();
  const SfmResult r = runIncrementalSfm(s.input, ReconstructionParams{});  // ba_every = 1
  EXPECT_EQ(r.rec.registeredCount(), 12);
  ASSERT_FALSE(r.ba_runs.empty());
  EXPECT_EQ(r.ba_runs[r.ba_runs.size() - 2].stage, "final");
  EXPECT_EQ(r.ba_runs.back().stage, "final_2");
  EXPECT_LT(r.ba_runs.back().rms_after_px, 0.6);
  for (const auto& [img, cam] : r.rec.cameras) EXPECT_NEAR(quaternionNorm(cam.pose.q), 1.0, 1e-9);
}
