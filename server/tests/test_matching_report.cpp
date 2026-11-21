// Pruebas del matching_report.json y del archivo de inliers (sección 3.8 del Sprint 2).
#include "matching_report.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path tempPath(const std::string& name) {
  return fs::temp_directory_path() /
         (name + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

MatchingReport sampleReport(int n_views = 3) {
  MatchingReport r;
  r.session_id = "20261120-212147-llama";
  r.generated_at = "2026-11-20T22:00:00-04:00";
  for (int k = 1; k <= n_views; ++k) {
    ViewReport v;
    v.index = k;
    v.file = "img_00" + std::to_string(k) + ".jpg";
    v.features_file = "features/img_00" + std::to_string(k) + ".feat";
    v.n_features = 7000 + k;
    v.work_width = 2000;
    v.work_height = 1500;
    v.intrinsics.focal_px = 2888.8;
    v.intrinsics.cx = 2000;
    v.intrinsics.cy = 1500;
    v.intrinsics.width = 4000;
    v.intrinsics.height = 3000;
    v.intrinsics.used_fallback = (k == 3);
    v.extract_ms = 812.5;
    r.views.push_back(v);
  }
  r.excluded_views.push_back({9, "rechazada_calidad"});

  PairReport good;
  good.i = 1;
  good.j = 2;
  good.angle_deg = 18.2;
  good.n_matches = 1500;
  good.n_inliers = 1200;
  good.inlier_ratio = 0.8;
  good.mean_sampson_px = 0.31;
  good.accepted = true;
  good.F = cv::Matx33d(0, -1e-6, 1e-3, 1e-6, 0, -2e-3, -1e-3, 2e-3, 1);
  good.E = cv::Matx33d(0, -0.1, 0.2, 0.1, 0, -0.7, -0.2, 0.7, 0);
  good.match_ms = 950;
  good.geometry_ms = 12;
  good.inliers_file = inliersFileName(1, 2);

  PairReport bad;
  bad.i = 1;
  bad.j = 3;
  bad.angle_deg = 36.0;
  bad.n_matches = 40;
  bad.n_inliers = 12;
  bad.inlier_ratio = 0.3;
  bad.accepted = false;
  bad.reasons = {"pocos_inliers"};
  bad.F = good.F;
  bad.E = good.E;

  PairReport no_f;
  no_f.i = 2;
  no_f.j = 3;
  no_f.angle_deg = 18.0;
  no_f.n_matches = 5;
  no_f.reasons = {"f_no_estimada", "pocos_inliers", "razon_inliers_baja"};

  r.pairs = {good, bad, no_f};
  r.warnings = {"la foto 3 no trae focal_35mm: f = 1,2 x lado mayor"};
  r.extraction_ms = 2437.5;
  r.matching_ms = 1900;
  r.total_ms = 4400;
  return r;
}

}  // namespace

TEST(MatchingReport, JsonHasRequiredPairFields) {
  const json j = toJson(sampleReport());
  EXPECT_EQ(j.at("schema_version"), 1);
  EXPECT_EQ(j.at("params").at("ratio"), 0.75);
  ASSERT_EQ(j.at("pairs").size(), 3u);
  for (const char* key : {"i", "j", "angle_deg", "n_matches", "n_inliers", "inlier_ratio", "mean_sampson_px",
                          "accepted", "reasons", "F", "E", "inliers_file"})
    EXPECT_TRUE(j.at("pairs").at(0).contains(key)) << key;
  EXPECT_EQ(j.at("pairs").at(0).at("F").size(), 3u);
  EXPECT_DOUBLE_EQ(j.at("pairs").at(0).at("F").at(2).at(2).get<double>(), 1.0);
  EXPECT_TRUE(j.at("pairs").at(2).at("F").is_null());  // F no estimada
  const json& K = j.at("views").at(0).at("K");
  EXPECT_DOUBLE_EQ(K.at(0).at(2).get<double>(), 2000.0);
  EXPECT_EQ(j.at("views").at(2).at("focal_source"), "fallback");
  EXPECT_EQ(j.at("views").at(0).at("focal_source"), "focal_35mm");
}

TEST(MatchingReport, SummaryComparesWithAllVsAll) {
  const json s = toJson(sampleReport(20)).at("summary");
  EXPECT_EQ(s.at("views_used"), 20);
  EXPECT_EQ(s.at("views_excluded"), 1);
  EXPECT_EQ(s.at("pairs_all_vs_all"), 190);
  EXPECT_EQ(s.at("pairs_selected"), 3);
  EXPECT_EQ(s.at("pairs_accepted"), 1);
  EXPECT_EQ(s.at("pairs_rejected"), 2);
}

TEST(MatchingReport, RejectedPairIsNotMarkedForSprint3) {
  const json j = toJson(sampleReport());
  const json& bad = j.at("pairs").at(1);
  EXPECT_FALSE(bad.at("accepted").get<bool>());
  EXPECT_TRUE(bad.at("inliers_file").is_null());
  EXPECT_EQ(bad.at("reasons"), json::array({"pocos_inliers"}));
  // Aunque se le hubiera puesto un archivo por error, la salida no lo publica.
  MatchingReport r = sampleReport();
  r.pairs[1].inliers_file = "matches/pair_001_003.csv";
  EXPECT_TRUE(toJson(r).at("pairs").at(1).at("inliers_file").is_null());
}

TEST(MatchingReport, RoundTripThroughFile) {
  const fs::path file = tempPath("escaner3d_report_") += ".json";
  writeMatchingReport(file, sampleReport());
  EXPECT_FALSE(fs::exists(fs::path(file.string() + ".tmp")));
  const MatchingReport back = readMatchingReport(file);
  fs::remove(file);
  EXPECT_EQ(toJson(back), toJson(sampleReport()));
  EXPECT_FALSE(back.pairs[2].F.has_value());
  EXPECT_TRUE(back.views[2].intrinsics.used_fallback);
}

TEST(MatchingReport, MissingRequiredFieldsThrow) {
  json j = toJson(sampleReport());
  j.at("pairs").at(0).erase("n_inliers");
  EXPECT_THROW(matchingReportFromJson(j), std::invalid_argument);
  EXPECT_THROW(matchingReportFromJson(json::object()), std::invalid_argument);
}

TEST(Inliers, FileNameIsZeroPadded) { EXPECT_EQ(inliersFileName(1, 20), "matches/pair_001_020.csv"); }

TEST(Inliers, RoundTripIsExact) {
  std::vector<InlierCorrespondence> in;
  for (int k = 0; k < 50; ++k) {
    InlierCorrespondence c;
    c.index_i = k * 3;
    c.index_j = 7000 - k;
    c.point_i = {1234.5678f + k / 3.0f, 987.654321f - k};
    c.point_j = {0.1f * k, 2999.999f};
    c.distance = 101.25f + k / 7.0f;
    c.sampson_px = 0.123456789012345 * k;
    in.push_back(c);
  }
  const fs::path file = tempPath("escaner3d_inliers_") += ".csv";
  writeInliers(file, in);
  const std::vector<InlierCorrespondence> out = readInliers(file);
  fs::remove(file);
  ASSERT_EQ(out.size(), in.size());
  for (std::size_t k = 0; k < in.size(); ++k) {
    EXPECT_EQ(out[k].index_i, in[k].index_i);
    EXPECT_EQ(out[k].index_j, in[k].index_j);
    EXPECT_EQ(out[k].point_i, in[k].point_i);
    EXPECT_EQ(out[k].point_j, in[k].point_j);
    EXPECT_EQ(out[k].distance, in[k].distance);
    EXPECT_DOUBLE_EQ(out[k].sampson_px, in[k].sampson_px);
  }
}

TEST(Inliers, InvalidFileThrows) {
  const fs::path file = tempPath("escaner3d_inliers_bad_") += ".csv";
  {
    std::ofstream out(file);
    out << "index_i,index_j,x_i,y_i,x_j,y_j,distance,sampson_px\n1,2,tres\n";
  }
  EXPECT_THROW(readInliers(file), std::runtime_error);
  fs::remove(file);
  EXPECT_THROW(readInliers(file), std::runtime_error);
}
