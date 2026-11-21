// Pruebas de las salidas del Epic 3: reconstruction.json, PLY y comprobaciones del reporte.
#include "pairing.h"
#include "reconstruction_io.h"
#include "test_sfm_scene.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

ReconstructionFile sample() {
  ReconstructionFile f;
  f.session_id = "20261121-142259-tenis";
  f.generated_at = "2026-11-21T15:00:00-04:00";
  f.params.min_views = 4;
  Reconstruction& rec = f.rec;
  rec.camera = scene::camera(2888.84);
  rec.camera.k1 = -0.031;
  rec.camera.k2 = 0.0042;
  rec.cameras[1] = {1, true, "", Pose{}};
  rec.cameras[2] = {2, true, "", scene::lookAt({1, -5, 0.3})};
  rec.cameras[9] = {9, false, "componente_desconectado", Pose{}};
  for (int k = 0; k < 3; ++k) {
    Point3D p;
    p.xyz = cv::Vec3d(0.1 * k, -0.2, 3.0 + k);
    p.rgb = cv::Vec3b(static_cast<uchar>(10 * k), 200, 30);
    p.observations = {{1, 100 + k, {123.25 + k, 456.5}}, {2, 200 + k, {321.125, 654.75 - k}}};
    rec.points.push_back(p);
  }
  return f;
}

fs::path tempFile(const std::string& name) {
  return fs::temp_directory_path() /
         (name + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

}  // namespace

TEST(ReconstructionIo, JsonHasConventionsAndRequiredFields) {
  const json j = toJson(sample());
  EXPECT_EQ(j.at("schema_version"), 1);
  EXPECT_TRUE(j.at("conventions").contains("quaternion"));
  EXPECT_DOUBLE_EQ(j.at("intrinsics").at("f").get<double>(), 2888.84);
  EXPECT_DOUBLE_EQ(j.at("intrinsics").at("k2").get<double>(), 0.0042);
  ASSERT_EQ(j.at("cameras").size(), 3u);
  EXPECT_EQ(j.at("cameras").at(0).at("q").size(), 4u);
  EXPECT_TRUE(j.at("cameras").at(2).at("q").is_null());  // no registrada
  EXPECT_EQ(j.at("cameras").at(2).at("reason"), "componente_desconectado");
  const json& p = j.at("points").at(1);
  for (const char* key : {"id", "xyz", "rgb", "track_length", "error_px", "observations"})
    EXPECT_TRUE(p.contains(key)) << key;
  EXPECT_EQ(p.at("track_length"), 2);
  EXPECT_EQ(p.at("rgb"), json::array({10, 200, 30}));
}

TEST(ReconstructionIo, RoundTripThroughFile) {
  const fs::path file = tempFile("escaner3d_rec_") += ".json";
  writeReconstruction(file, sample());
  EXPECT_FALSE(fs::exists(fs::path(file.string() + ".tmp")));
  const ReconstructionFile back = readReconstruction(file);
  fs::remove(file);
  EXPECT_EQ(toJson(back), toJson(sample()));
  EXPECT_EQ(back.params.min_views, 4);
  EXPECT_EQ(back.rec.points[2].observations[1].point, 202);
  EXPECT_FALSE(back.rec.cameras.at(9).registered);
}

TEST(ReconstructionIo, MissingFieldsThrow) {
  json j = toJson(sample());
  j.erase("intrinsics");
  EXPECT_THROW(reconstructionFromJson(j), std::invalid_argument);
}

TEST(ReconstructionIo, PlyHeaderAndVertexCount) {
  const std::string ply = toPly(sample().rec);
  std::istringstream in(ply);
  std::string line;
  std::getline(in, line);
  EXPECT_EQ(line, "ply");
  std::getline(in, line);
  EXPECT_EQ(line, "format ascii 1.0");
  EXPECT_NE(ply.find("element vertex 5\n"), std::string::npos);  // 3 puntos + 2 cámaras registradas
  for (const char* prop : {"property float x", "property float y", "property float z", "property uchar red",
                           "property uchar green", "property uchar blue"})
    EXPECT_NE(ply.find(prop), std::string::npos) << prop;
  const std::size_t body = ply.find("end_header\n");
  ASSERT_NE(body, std::string::npos);
  int vertices = 0;
  std::istringstream rest(ply.substr(body + 11));
  while (std::getline(rest, line))
    if (!line.empty()) ++vertices;
  EXPECT_EQ(vertices, 5);
  EXPECT_NE(ply.find(" 255 0 255"), std::string::npos);  // centros de cámara en magenta
}

TEST(ReconstructionIo, ImuConsistencyIsZeroForExactOrientations) {
  // La IMU da la orientación del dispositivo D = Rᵀ·M, con M un montaje fijo desconocido.
  const scene::SfmScene s = scene::makeRingScene();
  Reconstruction rec;
  std::map<int, Quat> imu;
  const cv::Matx33d M = quaternionToRotation({0.7, 0.1, -0.7, 0.2});
  for (const auto& [img, pose] : s.poses) {
    rec.cameras[img] = {img, true, "", pose};
    imu[img] = rotationToQuaternion(pose.R().t() * M);
  }
  const json j = imuConsistencyJson(rec, imu);
  EXPECT_EQ(j.at("pairs").size(), 11u);
  EXPECT_LT(j.at("max_abs_diff_deg").get<double>(), 1e-6);
  EXPECT_NEAR(j.at("pairs").at(0).at("reconstruction_deg").get<double>(), 30.0, 1.0);
  // Con una IMU desviada 3° en una foto, la diferencia aparece. Solo se compara la
  // magnitud de la rotación relativa, así que un error de 3° cambia esa magnitud en
  // menos de 3° (depende del eje del error frente al eje del giro entre fotos).
  imu[5] = rotationToQuaternion(quaternionToRotation({std::cos(0.0261799), std::sin(0.0261799), 0, 0}) *
                                quaternionToRotation(imu[5]));
  const double diff = imuConsistencyJson(rec, imu).at("max_abs_diff_deg").get<double>();
  EXPECT_GT(diff, 0.1);
  EXPECT_LE(diff, 3.0 + 1e-6);
}

TEST(ReconstructionIo, RingGeometry) {
  const scene::SfmScene s = scene::makeRingScene();
  Reconstruction rec;
  for (const auto& [img, pose] : s.poses) rec.cameras[img] = {img, true, "", pose};
  rec.cameras[99] = {99, false, "x", Pose{}};  // no cuenta
  const json j = ringGeometryJson(rec);
  EXPECT_EQ(j.at("cameras"), 12);
  // Centros a 6 del eje con alturas 1,5 ± 0,5: distancia al centroide ≈ 6.
  EXPECT_NEAR(j.at("mean_distance").get<double>(), 6.0, 0.05);
  EXPECT_LT(j.at("std_distance").get<double>(), 0.05);
}

TEST(ReconstructionIo, PipelineFixesScaleWithInitialPair) {
  const scene::SfmScene s = scene::makeRingScene();
  const SfmResult r = runIncrementalSfm(s.input, ReconstructionParams{});
  const cv::Vec3d ci = r.rec.cameras.at(r.init.i).pose.center();
  const cv::Vec3d cj = r.rec.cameras.at(r.init.j).pose.center();
  EXPECT_NEAR(cv::norm(cj - ci), 1.0, 1e-9);
  EXPECT_LT(cv::norm(ci), 1e-9);  // la primera cámara queda en el origen
}
