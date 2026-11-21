// Prueba de extremo a extremo del Epic 2 con una sesión sintética en disco.
#include "manifest.h"
#include "session_matching.h"
#include "test_images.h"

#include <gtest/gtest.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::array<double, 4> aroundY(double deg) {
  const double h = deg * kPi / 360.0;
  return {std::cos(h), 0.0, std::sin(h), 0.0};
}

class SessionMatchingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = fs::temp_directory_path() /
           ("escaner3d_session_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir_);

    // Foto 1 y 2: la misma textura con un pequeño giro (par válido).
    // Foto 3: otra textura sin relación, orientada cerca (par seleccionado pero inválido).
    // Foto 4: rechazada por calidad en el Sprint 1 (no se usa).
    // Foto 5: sin orientación (no se usa).
    const cv::Mat a = testimg::blobTexture(800, 600, 101);
    cv::Mat b;
    const cv::Mat m = cv::getRotationMatrix2D(cv::Point2f(400, 300), 6.0, 0.95);
    cv::warpAffine(a, b, m, a.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT);
    const cv::Mat c = testimg::blobTexture(800, 600, 202);
    const std::vector<cv::Mat> images = {a, b, c, a, b};

    Manifest manifest;
    manifest.session_id = dir_.filename().string();
    manifest.created_at = "2026-11-20T21:21:47-04:00";
    for (int k = 0; k < 5; ++k) {
      PhotoRecord p;
      p.metadata.index = k + 1;
      p.metadata.width = 800;
      p.metadata.height = 600;
      p.metadata.focal_35mm = 25.0;
      if (k != 4) p.metadata.orientation_q = aroundY(10.0 * k);
      p.file = photoFileName(k + 1);
      p.quality.accepted = (k != 3);
      if (k == 3) p.quality.reasons = {"borrosa"};
      cv::imwrite((dir_ / p.file).string(), images[k], {cv::IMWRITE_JPEG_QUALITY, 95});
      manifest.photos.push_back(p);
    }
    writeManifestAtomic(dir_ / "manifest.json", manifest);
  }
  void TearDown() override { fs::remove_all(dir_); }

  fs::path dir_;
};

}  // namespace

TEST_F(SessionMatchingTest, ProducesFeaturesReportAndInliersOnlyForAcceptedPairs) {
  // Restos de una corrida anterior: no deben sobrevivir como si fueran aceptados.
  fs::create_directories(dir_ / "matches");
  std::ofstream(dir_ / "matches" / "pair_001_003.csv") << "viejo\n";

  const MatchingReport r = runSessionMatching(dir_, MatchingParams{});

  ASSERT_EQ(r.views.size(), 3u);
  for (const ViewReport& v : r.views) {
    EXPECT_TRUE(fs::exists(dir_ / v.features_file)) << v.features_file;
    EXPECT_GT(v.n_features, 100);
    EXPECT_FALSE(v.intrinsics.used_fallback);
  }
  ASSERT_EQ(r.excluded_views.size(), 2u);
  EXPECT_EQ(r.excluded_views[0].index, 4);
  EXPECT_EQ(r.excluded_views[0].reason, "rechazada_calidad");
  EXPECT_EQ(r.excluded_views[1].index, 5);
  EXPECT_EQ(r.excluded_views[1].reason, "sin_orientacion");

  ASSERT_EQ(r.pairs.size(), 3u);  // (1,2), (1,3), (2,3)
  const PairReport& good = r.pairs[0];
  EXPECT_EQ(good.i, 1);
  EXPECT_EQ(good.j, 2);
  EXPECT_TRUE(good.accepted) << (good.reasons.empty() ? "" : good.reasons[0]);
  EXPECT_TRUE(fs::exists(dir_ / good.inliers_file));
  for (std::size_t k = 1; k < r.pairs.size(); ++k) {
    EXPECT_FALSE(r.pairs[k].accepted) << r.pairs[k].i << "-" << r.pairs[k].j;
    EXPECT_FALSE(r.pairs[k].reasons.empty());
    EXPECT_TRUE(r.pairs[k].inliers_file.empty());
  }
  EXPECT_FALSE(fs::exists(dir_ / "matches" / "pair_001_003.csv"));
  EXPECT_FALSE(fs::exists(dir_ / "matches" / "pair_002_003.csv"));

  const std::vector<InlierCorrespondence> inliers = readInliers(dir_ / good.inliers_file);
  EXPECT_EQ(static_cast<int>(inliers.size()), good.n_inliers);

  const MatchingReport back = readMatchingReport(dir_ / "matching_report.json");
  EXPECT_EQ(toJson(back), toJson(r));
  const MatchingSummary s = summarize(back);
  EXPECT_EQ(s.pairs_all_vs_all, 3);
  EXPECT_EQ(s.pairs_accepted, 1);
  EXPECT_EQ(s.pairs_rejected, 2);
}

TEST_F(SessionMatchingTest, DebugWritesMatchImages) {
  SessionMatchingOptions options;
  options.debug = true;
  runSessionMatching(dir_, MatchingParams{}, options);
  EXPECT_TRUE(fs::exists(dir_ / "debug" / "accepted_001_002.jpg"));
  bool rejected_image = false;
  for (const auto& e : fs::directory_iterator(dir_ / "debug"))
    rejected_image |= e.path().filename().string().rfind("rejected_", 0) == 0;
  EXPECT_TRUE(rejected_image);
}

TEST(SessionMatching, MissingSessionThrows) {
  EXPECT_THROW(runSessionMatching(fs::temp_directory_path() / "escaner3d_no_existe_xyz", MatchingParams{}),
               std::runtime_error);
}
