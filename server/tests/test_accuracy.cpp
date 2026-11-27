// Pruebas de HU-24: alineación por similitud y reporte de precisión frente a COLMAP.
#include "accuracy.h"
#include "test_sfm_scene.h"

#include <gtest/gtest.h>
#include <opencv2/calib3d.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

cv::Matx33d rotation(double deg, cv::Vec3d axis) {
  cv::Mat R;
  cv::Rodrigues(cv::normalize(axis) * (deg * scene::kPi / 180.0), R);
  return cv::Matx33d(R);
}

std::vector<cv::Vec3d> randomCenters(int n, unsigned seed) {
  cv::RNG rng(seed);
  std::vector<cv::Vec3d> v;
  for (int k = 0; k < n; ++k) v.emplace_back(rng.uniform(-5.0, 5.0), rng.uniform(-5.0, 5.0), rng.uniform(-2.0, 2.0));
  return v;
}

// Reconstrucción "propia" y la misma vista por COLMAP en otro sistema (similitud conocida).
struct TwoModels {
  Reconstruction ours, colmap;
  Similarity3 truth;
};

TwoModels twoModels(int n_cameras) {
  TwoModels t;
  t.truth.s = 2.5;
  t.truth.R = rotation(40, {0.2, 1, 0.3});
  t.truth.t = {3, -1, 7};
  const std::vector<Pose> ring = scene::ring(n_cameras, 6.0, 360.0 / n_cameras);
  for (int k = 0; k < n_cameras; ++k) {
    const Pose& p = ring[static_cast<std::size_t>(k)];
    t.ours.cameras[k + 1] = {k + 1, true, "", p};
    // En el mundo de COLMAP: C' = s·R·C + t y R_cam' = R_cam·Rᵀ.
    const cv::Vec3d C = t.truth.apply(p.center());
    const cv::Matx33d Rc = p.R() * t.truth.R.t();
    t.colmap.cameras[k + 1] = {k + 1, true, "", Pose::fromRt(Rc, -(Rc * C))};
  }
  t.ours.camera = scene::camera(3079);
  t.colmap.camera = scene::camera(3093);
  for (const cv::Vec3d& X : scene::randomPoints(50, 3, 1.0)) {
    Point3D p;
    p.xyz = X;
    t.ours.points.push_back(p);
    Point3D q;
    q.xyz = t.truth.apply(X);
    t.colmap.points.push_back(q);
  }
  return t;
}

}  // namespace

TEST(Accuracy, UmeyamaRecoversKnownSimilarityExactly) {
  Similarity3 truth;
  truth.s = 0.37;
  truth.R = rotation(123, {1, -2, 0.5});
  truth.t = {10, 20, -5};
  const std::vector<cv::Vec3d> from = randomCenters(10, 1);
  std::vector<cv::Vec3d> to;
  for (const cv::Vec3d& x : from) to.push_back(truth.apply(x));
  const Similarity3 est = umeyama(from, to);
  EXPECT_NEAR(est.s, truth.s, 1e-6);
  EXPECT_LT(cv::norm(est.R - truth.R), 1e-6);
  EXPECT_LT(cv::norm(est.t - truth.t), 1e-6);
}

TEST(Accuracy, UmeyamaWithNoiseHasSmallError) {
  Similarity3 truth;
  truth.s = 2;
  truth.R = rotation(30, {0, 0, 1});
  const std::vector<cv::Vec3d> from = randomCenters(30, 2);
  cv::RNG rng(5);
  std::vector<cv::Vec3d> to;
  for (const cv::Vec3d& x : from) to.push_back(truth.apply(x) + cv::Vec3d(rng.gaussian(0.01), rng.gaussian(0.01), rng.gaussian(0.01)));
  const Similarity3 est = umeyama(from, to);
  EXPECT_NEAR(est.s, 2.0, 0.01);
  EXPECT_LT(rotationDifferenceDeg(est.R, truth.R), 0.2);
}

TEST(Accuracy, RansacIgnoresOutliers) {
  Similarity3 truth;
  truth.s = 1.5;
  truth.R = rotation(70, {1, 1, 0});
  truth.t = {1, 2, 3};
  const std::vector<cv::Vec3d> from = randomCenters(20, 7);
  std::vector<cv::Vec3d> to;
  for (const cv::Vec3d& x : from) to.push_back(truth.apply(x));
  to[3] += cv::Vec3d(9, 0, 0);
  to[11] += cv::Vec3d(0, -7, 4);
  std::vector<int> inliers;
  const Similarity3 est = umeyamaRansac(from, to, 200, 0.5, inliers);
  EXPECT_EQ(inliers.size(), 18u);
  EXPECT_EQ(std::count(inliers.begin(), inliers.end(), 3), 0);
  EXPECT_NEAR(est.s, 1.5, 1e-6);
  EXPECT_LT(cv::norm(est.t - truth.t), 1e-6);
}

TEST(Accuracy, RotationDifferenceHandComputed) {
  EXPECT_NEAR(rotationDifferenceDeg(rotation(25, {0, 0, 1}), cv::Matx33d::eye()), 25.0, 1e-9);
  EXPECT_NEAR(rotationDifferenceDeg(rotation(10, {1, 0, 0}), rotation(10, {1, 0, 0})), 0.0, 1e-6);
}

TEST(Accuracy, IdenticalModelsUpToSimilarityHaveZeroError) {
  TwoModels t = twoModels(12);
  // Una cámara con 5° de error de rotación (mismo centro) en el modelo propio.
  Pose& p5 = t.ours.cameras[5].pose;
  const cv::Vec3d C5 = p5.center();
  const cv::Matx33d R5 = rotation(5, {0, 1, 0}) * p5.R();
  p5 = Pose::fromRt(R5, -(R5 * C5));
  const AccuracyReport r = compareReconstructions(t.ours, t.colmap, AccuracyParams{});
  EXPECT_TRUE(r.reliable);
  EXPECT_EQ(r.common, 12);
  EXPECT_NEAR(r.scale, 2.5, 1e-6);
  EXPECT_NEAR(r.ring_radius, 2.5 * 6.0, 0.2);
  EXPECT_LT(r.position_max, 1e-6 * 15);
  for (const CameraAccuracy& c : r.cameras)
    EXPECT_NEAR(c.rotation_error_deg, c.index == 5 ? 5.0 : 0.0, 1e-4) << c.index;  // acos cerca de 1: ~1e-6°
  EXPECT_NEAR(r.rotation_max_deg, 5.0, 1e-6);
  EXPECT_DOUBLE_EQ(r.f_ours, 3079);
  EXPECT_DOUBLE_EQ(r.f_colmap, 3093);
  EXPECT_NEAR(r.point_distance_mean, 0.0, 1e-6);
  EXPECT_EQ(r.points_ours, 50);
}

TEST(Accuracy, FewCommonCamerasIsNotReliable) {
  TwoModels t = twoModels(12);
  for (int k = 4; k <= 12; ++k) t.colmap.cameras[k].registered = false;
  const AccuracyReport r = compareReconstructions(t.ours, t.colmap, AccuracyParams{});
  EXPECT_EQ(r.common, 3);
  EXPECT_FALSE(r.reliable);
  EXPECT_FALSE(r.note.empty());
  const nlohmann::json j = toJson(r);
  EXPECT_TRUE(j.at("alignment").at("position_error").is_null());
  EXPECT_TRUE(j.at("alignment").at("rotation_error_deg").is_null());
}

TEST(Accuracy, JsonHasCompleteStructure) {
  const AccuracyReport r = compareReconstructions(twoModels(10).ours, twoModels(10).colmap, AccuracyParams{});
  const nlohmann::json j = toJson(r);
  for (const char* key : {"cameras_ours", "cameras_colmap", "cameras_common", "reliable", "note", "alignment",
                          "intrinsics", "reprojection_error_px", "sparse_points", "per_camera"})
    EXPECT_TRUE(j.contains(key)) << key;
  for (const char* key : {"scale", "ring_radius", "position_error", "position_error_pct_radius", "rotation_error_deg", "inliers"})
    EXPECT_TRUE(j.at("alignment").contains(key)) << key;
  EXPECT_TRUE(j.at("alignment").at("position_error").contains("rms"));
  EXPECT_TRUE(j.at("intrinsics").contains("f_diff_pct"));
  EXPECT_TRUE(j.at("reprojection_error_px").contains("colmap"));
  EXPECT_EQ(j.at("per_camera").size(), 10u);
}

TEST(Accuracy, GeneratesColmapReferenceWithExpectedCommands) {
  const fs::path dir = fs::temp_directory_path() /
                       ("escaner3d_ref_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(dir);
  _putenv_s("COLMAP_STUB_LOG", (dir / "args.txt").string().c_str());
  ColmapRunner colmap(COLMAP_STUB_PATH, dir / "colmap.log");
  const int n = generateColmapReference(colmap, dir / "fotos", {"img_001.jpg", "img_002.jpg"}, dir / "colmap_ref");
  EXPECT_EQ(n, 2);  // el modelo de texto del stub tiene 2 imágenes
  std::string args;
  {
    std::ifstream in(dir / "args.txt");
    std::stringstream ss;
    ss << in.rdbuf();
    args = ss.str();
  }
  const std::size_t fe = args.find("feature_extractor"), em = args.find("exhaustive_matcher"), mp = args.find("mapper"),
                    mc = args.find("model_converter");
  ASSERT_NE(fe, std::string::npos);
  ASSERT_NE(mc, std::string::npos);
  EXPECT_LT(fe, em);
  EXPECT_LT(em, mp);
  EXPECT_LT(mp, mc);
  EXPECT_NE(args.find("--ImageReader.single_camera\n1"), std::string::npos);
  EXPECT_NE(args.find("--ImageReader.camera_model\nRADIAL"), std::string::npos);
  EXPECT_NE(args.find("--image_list_path"), std::string::npos);
  EXPECT_NE(args.find("--output_type\nTXT"), std::string::npos);
  EXPECT_TRUE(fs::exists(dir / "colmap_ref" / "sparse_txt" / "images.txt"));
  {
    std::ifstream list(dir / "colmap_ref" / "image_list.txt");
    std::string first;
    std::getline(list, first);
    EXPECT_EQ(first, "img_001.jpg");
  }
  fs::remove_all(dir);
}
