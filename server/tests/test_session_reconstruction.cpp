// Prueba de extremo a extremo del Epic 3 con una sesión sintética en disco
// (manifiesto, matching_report.json, matches/*.csv y fotos de color conocido).
#include "manifest.h"
#include "matching_report.h"
#include "reconstruction_io.h"
#include "session_reconstruction.h"
#include "test_sfm_scene.h"

#include <gtest/gtest.h>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

class SessionReconstructionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = fs::temp_directory_path() /
           ("escaner3d_rec_session_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir_ / "matches");
    const scene::SfmScene s = scene::makeRingScene();
    const cv::Mat photo(s.camera.height, s.camera.width, CV_8UC3, cv::Scalar(40, 120, 220));  // BGR

    Manifest manifest;
    manifest.session_id = "20261121-000000-sintetica";
    manifest.created_at = "2026-11-21T00:00:00-04:00";
    MatchingReport matching;
    matching.session_id = manifest.session_id;
    matching.generated_at = manifest.created_at;
    for (int img = 1; img <= 13; ++img) {
      PhotoRecord p;
      p.metadata.index = img;
      p.metadata.width = s.camera.width;
      p.metadata.height = s.camera.height;
      p.metadata.focal_35mm = 25.0;
      p.file = photoFileName(img);
      p.quality.accepted = img != 13;  // la 13 quedó borrosa en el Sprint 1
      if (img <= 12) {
        // IMU: orientación del dispositivo con un montaje fijo (solo la magnitud importa).
        p.metadata.orientation_q = rotationToQuaternion(s.poses.at(img).R().t());
        ViewReport v;
        v.index = img;
        v.file = p.file;
        v.features_file = "features/img.feat";
        v.intrinsics.focal_px = s.camera.f;
        v.intrinsics.cx = s.camera.cx;
        v.intrinsics.cy = s.camera.cy;
        v.intrinsics.width = s.camera.width;
        v.intrinsics.height = s.camera.height;
        matching.views.push_back(v);
      } else {
        matching.excluded_views.push_back({img, "rechazada_calidad"});
      }
      cv::imwrite((dir_ / p.file).string(), photo);
      manifest.photos.push_back(p);
    }
    for (const PairCandidate& pc : s.input.pairs) {
      PairReport pr;
      pr.i = pc.i;
      pr.j = pc.j;
      pr.accepted = true;
      pr.n_matches = pr.n_inliers = pc.n_inliers();
      pr.E = pc.E;
      pr.F = pc.E;
      pr.inliers_file = inliersFileName(pc.i, pc.j);
      std::vector<InlierCorrespondence> inl;
      // Las coordenadas de la escena están en s.observed; el índice del punto es el del punto 3D.
      for (const auto& [k, xy] : s.observed.at(pc.i)) {
        const auto it = s.observed.at(pc.j).find(k);
        if (it == s.observed.at(pc.j).end()) continue;
        InlierCorrespondence c;
        c.index_i = c.index_j = k;
        c.point_i = cv::Point2f(xy);
        c.point_j = cv::Point2f(it->second);
        inl.push_back(c);
      }
      writeInliers(dir_ / pr.inliers_file, inl);
      matching.pairs.push_back(pr);
    }
    writeManifestAtomic(dir_ / "manifest.json", manifest);
    writeMatchingReport(dir_ / "matching_report.json", matching);
  }
  void TearDown() override { fs::remove_all(dir_); }

  fs::path dir_;
};

}  // namespace

TEST_F(SessionReconstructionTest, WritesOutputsAndReport) {
  SessionReconstructionOptions o;
  o.debug = true;
  const SessionReconstructionResult r = runSessionReconstruction(dir_, ReconstructionParams{}, o);
  EXPECT_EQ(r.sfm.rec.registeredCount(), 12);
  const fs::path out = dir_ / "reconstruction";
  for (const char* f : {"reconstruction.json", "reconstruction_report.json", "sparse_cloud.ply", "debug_top.jpg",
                        "debug_side.jpg"})
    EXPECT_TRUE(fs::exists(out / f)) << f;

  std::ifstream in(out / "reconstruction_report.json");
  const json report = json::parse(in);
  EXPECT_EQ(report.at("summary").at("cameras_registered"), 12);
  EXPECT_EQ(report.at("summary").at("cameras_candidates"), 12);
  ASSERT_EQ(report.at("excluded_photos").size(), 1u);
  EXPECT_EQ(report.at("excluded_photos").at(0).at("index"), 13);
  EXPECT_EQ(report.at("excluded_photos").at(0).at("reason"), "rechazada_calidad");
  EXPECT_LT(report.at("global").at("after").at("rms_px").get<double>(), 0.6);
  EXPECT_LT(report.at("imu_consistency").at("max_abs_diff_deg").get<double>(), 0.5);
  EXPECT_TRUE(report.at("timing_ms").contains("total"));

  const ReconstructionFile rec = readReconstruction(out / "reconstruction.json");
  ASSERT_FALSE(rec.rec.points.empty());
  const cv::Vec3b rgb = rec.rec.points.front().rgb;  // foto de color BGR (40, 120, 220)
  EXPECT_NEAR(rgb[0], 220, 3);
  EXPECT_NEAR(rgb[1], 120, 3);
  EXPECT_NEAR(rgb[2], 40, 3);
}

TEST_F(SessionReconstructionTest, IncludeRejectedAddsPhotoAsUnregisteredCandidate) {
  SessionReconstructionOptions o;
  o.include_rejected = true;
  const SessionReconstructionResult r = runSessionReconstruction(dir_, ReconstructionParams{}, o);
  EXPECT_EQ(r.report.params.exclude_rejected_photos, false);
  ASSERT_TRUE(r.sfm.rec.cameras.count(13));
  EXPECT_FALSE(r.sfm.rec.cameras.at(13).registered);
  EXPECT_EQ(r.sfm.rec.cameras.at(13).reason, "componente_desconectado");
  EXPECT_FALSE(r.report.warnings.empty());
}

TEST(SessionReconstruction, MissingMatchingReportThrows) {
  const fs::path dir = fs::temp_directory_path() / "escaner3d_rec_sin_matching";
  fs::create_directories(dir);
  std::ofstream(dir / "manifest.json") << "{}";
  EXPECT_THROW(runSessionReconstruction(dir, ReconstructionParams{}), std::runtime_error);
  fs::remove_all(dir);
  EXPECT_THROW(runSessionReconstruction(dir, ReconstructionParams{}), std::runtime_error);
}
