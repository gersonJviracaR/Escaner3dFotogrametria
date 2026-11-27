// Pruebas de las rutas y pasos por sesión del Epic 4.
#include "manifest.h"
#include "reconstruction_io.h"
#include "session_pipeline.h"
#include "test_scene.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

class SessionPipelineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = fs::temp_directory_path() /
           ("escaner3d_pipeline_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir_ / "reconstruction");
    Manifest m;
    m.session_id = "s";
    m.created_at = "2026-11-27T00:00:00-04:00";
    for (int k = 1; k <= 3; ++k) {
      PhotoRecord p;
      p.metadata.index = k;
      p.file = photoFileName(k);
      p.quality.accepted = true;
      m.photos.push_back(p);
    }
    writeManifestAtomic(dir_ / "manifest.json", m);

    ReconstructionFile f;
    f.session_id = "s";
    f.generated_at = m.created_at;
    f.rec.camera = scene::camera();
    f.rec.cameras[1] = {1, true, "", Pose{}};
    f.rec.cameras[2] = {2, true, "", scene::lookAt({1, -5, 0})};
    f.rec.cameras[3] = {3, false, "pocas_correspondencias_2d3d", Pose{}};
    Point3D p;
    p.xyz = {0, 0, 5};
    p.observations = {{1, 4, {500, 375}}, {2, 9, {480, 370}}};
    f.rec.points = {p};
    writeReconstruction(dir_ / "reconstruction" / "reconstruction.json", f);
  }
  void TearDown() override { fs::remove_all(dir_); }
  fs::path dir_;
};

}  // namespace

TEST(SessionPipeline, PathsForEachPoseSource) {
  const SessionPaths ours = sessionPaths("S", "ours");
  EXPECT_EQ(ours.sparse_model, fs::path("S") / "colmap_ours" / "sparse" / "0");
  EXPECT_EQ(ours.dense, fs::path("S") / "dense");
  EXPECT_EQ(ours.model, fs::path("S") / "model");
  const SessionPaths colmap = sessionPaths("S", "colmap");
  EXPECT_EQ(colmap.sparse_model, fs::path("S") / "colmap_ref" / "sparse_txt");
  EXPECT_EQ(colmap.dense, fs::path("S") / "dense_colmap");
  EXPECT_EQ(colmap.model, fs::path("S") / "model_colmap");
  EXPECT_THROW(sessionPaths("S", "otra"), std::invalid_argument);
}

TEST_F(SessionPipelineTest, ExportsOnlyRegisteredCamerasWithPhotoNames) {
  const fs::path out = dir_ / "colmap_ours" / "sparse" / "0";
  EXPECT_EQ(exportOursToColmap(dir_, out), 2);
  const ColmapModel m = readColmapText(out);
  ASSERT_EQ(m.images.size(), 2u);
  EXPECT_EQ(m.images[0].name, "img_001.jpg");
  EXPECT_EQ(m.images[1].name, "img_002.jpg");
  EXPECT_EQ(m.points.size(), 1u);
  EXPECT_EQ(photoNames(dir_).at(3), "img_003.jpg");
}

TEST_F(SessionPipelineTest, LoadPosesFromEitherSource) {
  const Reconstruction ours = loadPoses(dir_, "ours");
  EXPECT_EQ(ours.registeredCount(), 2);
  // La referencia de COLMAP: la misma exportación sirve como modelo de prueba.
  exportOursToColmap(dir_, dir_ / "colmap_ref" / "sparse_txt");
  const Reconstruction colmap = loadPoses(dir_, "colmap");
  EXPECT_EQ(colmap.registeredCount(), 2);
  EXPECT_TRUE(colmap.cameras.at(2).registered);
  EXPECT_NEAR(colmap.camera.cx, ours.camera.cx, 1e-9);
  EXPECT_THROW(loadPoses(dir_ / "no_existe", "ours"), std::runtime_error);
}

TEST_F(SessionPipelineTest, ModelReportSectionsAccumulate) {
  const fs::path model = dir_ / "model";
  updateModelReport(model, "dense", json{{"points", 123}});
  updateModelReport(model, "mesh", json{{"faces", 45}});
  updateModelReport(model, "dense", json{{"points", 124}});
  const json r = readModelReport(model);
  EXPECT_EQ(r.at("dense").at("points"), 124);
  EXPECT_EQ(r.at("mesh").at("faces"), 45);
  EXPECT_FALSE(fs::exists(model / "model_report.json.tmp"));
  EXPECT_TRUE(readModelReport(dir_ / "vacio").is_object());
}
