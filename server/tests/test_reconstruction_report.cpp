// Pruebas de HU-18: error de reproyección por cámara y estructura del reporte.
#include "reconstruction_report.h"
#include "test_sfm_scene.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

// Cámara identidad (foto 1) y otra desplazada (foto 2); dos puntos con errores conocidos.
Reconstruction handMade() {
  Reconstruction rec;
  rec.camera = scene::camera();
  rec.cameras[1] = {1, true, "", Pose{}};
  rec.cameras[2] = {2, true, "", Pose{{1, 0, 0, 0}, {-1, 0, 0}}};
  rec.cameras[3] = {3, false, "pnp_pocos_inliers", Pose{}};
  const cv::Vec3d A(0, 0, 5), B(0.5, 0.25, 4);
  auto obs = [&](int img, const cv::Vec3d& X, double dx, double dy) {
    const cv::Point2d u = rec.camera.project(rec.cameras[img].pose, X);
    return Observation{img, 0, {u.x + dx, u.y + dy}};
  };
  Point3D a;
  a.xyz = A;
  a.observations = {obs(1, A, 3, 0), obs(2, A, 0, 0)};  // errores 3 y 0
  Point3D b;
  b.xyz = B;
  b.observations = {obs(1, B, 0, 4), obs(2, B, 3, 4)};  // errores 4 y 5
  rec.points = {a, b};
  return rec;
}

}  // namespace

TEST(ReconstructionReport, PerCameraErrorsMatchHandComputation) {
  const CameraErrors e = computeCameraErrors(handMade());
  ASSERT_EQ(e.per_camera.size(), 2u);  // la cámara 3 no está registrada
  const ErrorStats& c1 = e.per_camera.at(1);
  EXPECT_EQ(c1.n, 2);
  EXPECT_NEAR(c1.mean, 3.5, 1e-9);
  EXPECT_NEAR(c1.rms, std::sqrt((9.0 + 16.0) / 2.0), 1e-9);
  EXPECT_NEAR(c1.max, 4.0, 1e-9);
  const ErrorStats& c2 = e.per_camera.at(2);
  EXPECT_NEAR(c2.mean, 2.5, 1e-9);
  EXPECT_NEAR(c2.rms, std::sqrt(25.0 / 2.0), 1e-9);
  EXPECT_NEAR(c2.max, 5.0, 1e-9);
  EXPECT_EQ(e.global.n, 4);
  EXPECT_NEAR(e.global.mean, 3.0, 1e-9);
  EXPECT_NEAR(e.global.rms, std::sqrt((9.0 + 0 + 16.0 + 25.0) / 4.0), 1e-9);
  EXPECT_NEAR(e.global.max, 5.0, 1e-9);
}

TEST(ReconstructionReport, DescribeComputesStatistics) {
  const Stats s = describe({4, 1, 3, 2});
  EXPECT_DOUBLE_EQ(s.min, 1);
  EXPECT_DOUBLE_EQ(s.max, 4);
  EXPECT_DOUBLE_EQ(s.median, 2.5);
  EXPECT_DOUBLE_EQ(s.mean, 2.5);
  EXPECT_DOUBLE_EQ(describe({}).max, 0);
}

TEST(ReconstructionReport, PipelineRecordsErrorsBeforeAndAfter) {
  const scene::SfmScene s = scene::makeRingScene();
  const SfmResult r = runIncrementalSfm(s.input, ReconstructionParams{});
  EXPECT_EQ(r.errors_before.per_camera.size(), 12u);
  EXPECT_EQ(r.errors_after.per_camera.size(), 12u);
  EXPECT_GT(r.errors_before.global.n, 0);
  EXPECT_LE(r.errors_after.global.rms, r.errors_before.global.rms + 1e-9);
  EXPECT_LT(r.errors_after.global.rms, 0.6);
}

TEST(ReconstructionReport, JsonHasCompleteStructure) {
  ReconstructionReport r;
  r.session_id = "20261121-142259-tenis";
  r.solver = "DENSE_SCHUR";
  r.initial_intrinsics = scene::camera();
  r.final_intrinsics = scene::camera(910);
  r.final_intrinsics.k1 = -0.05;
  CameraSummary c;
  c.index = 1;
  c.registered = true;
  c.before = {10, 0.5, 0.6, 1.9};
  c.after = {8, 0.3, 0.35, 1.2};
  r.cameras = {c, CameraSummary{9, false, "componente_desconectado", {}, {}}};
  r.global_before = c.before;
  r.global_after = c.after;
  r.ba_runs = {BundleReport{"final", "DENSE_SCHUR", 12, 100, 10, 0.9, 0.4, 2, 50, 300, 0, "CONVERGENCE", 120}};
  r.timing_ms = {{"total", 1000}};
  const nlohmann::json j = toJson(r);

  for (const char* key : {"schema_version", "session_id", "generated_at", "params", "solver", "intrinsics", "initial_pair",
                          "excluded_photos", "cameras", "global", "points", "bundle_adjustment", "imu_consistency",
                          "ring_geometry", "timing_ms", "warnings", "summary"})
    EXPECT_TRUE(j.contains(key)) << key;
  EXPECT_DOUBLE_EQ(j.at("intrinsics").at("initial").at("f").get<double>(), 900.0);
  EXPECT_DOUBLE_EQ(j.at("intrinsics").at("final").at("f").get<double>(), 910.0);
  EXPECT_DOUBLE_EQ(j.at("intrinsics").at("final").at("k1").get<double>(), -0.05);
  const nlohmann::json& cam = j.at("cameras").at(0);
  EXPECT_EQ(cam.at("index"), 1);
  EXPECT_DOUBLE_EQ(cam.at("before").at("rms_px").get<double>(), 0.6);
  EXPECT_DOUBLE_EQ(cam.at("after").at("mean_px").get<double>(), 0.3);
  EXPECT_EQ(cam.at("after").at("n"), 8);
  EXPECT_DOUBLE_EQ(cam.at("after").at("max_px").get<double>(), 1.2);
  EXPECT_EQ(j.at("cameras").at(1).at("reason"), "componente_desconectado");
  EXPECT_TRUE(j.at("cameras").at(1).at("after").is_null());  // no registrada: sin errores
  EXPECT_DOUBLE_EQ(j.at("global").at("before").at("rms_px").get<double>(), 0.6);
  EXPECT_DOUBLE_EQ(j.at("global").at("after").at("rms_px").get<double>(), 0.35);
  EXPECT_EQ(j.at("bundle_adjustment").at(0).at("stage"), "final");
  EXPECT_EQ(j.at("summary").at("cameras_registered"), 1);
  EXPECT_EQ(j.at("summary").at("cameras_candidates"), 2);
}
