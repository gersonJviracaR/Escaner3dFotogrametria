// Pruebas de la búsqueda de focal del par inicial (corrección posterior al Sprint 3,
// encontrada con la sesión llamav2): si la K inicial no triangula ningún punto del par
// inicial, se prueba una rejilla de factores de focal solo para ese par.
//
// Nota: en una escena sintética pinhole ideal, un error de focal de ±15 % (incluso de ±30 %,
// con distorsión radial o con pasos de 10°) NO deja al par inicial sin puntos: con F exacta,
// la pose de dos vistas absorbe casi todo el error de focal. Por eso el caso que reproduce
// el fallo usa los datos reales del par 9-10 de llamav2 (tests/data/llamav2_par_009_010.*):
// sus inliers, la E del Sprint 2 y la K inicial del EXIF.
#include "matching_report.h"
#include "test_sfm_scene.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

ReconstructionParams withSearch(bool on) {
  ReconstructionParams p;
  p.init_focal_search = on;
  p.ba_every = 1000000;  // solo interesa el par inicial; el BA final igual se ejecuta
  return p;
}

// SfmInput con un único par real (9-10 de llamav2).
SfmInput llamav2Pair() {
  const fs::path dir = ESCANER3D_TEST_DATA_DIR;
  std::ifstream in(dir / "llamav2_par_009_010.json");
  const nlohmann::json j = nlohmann::json::parse(in);
  SfmInput input;
  input.camera.f = j.at("f").get<double>();
  input.camera.cx = j.at("cx").get<double>();
  input.camera.cy = j.at("cy").get<double>();
  input.camera.width = j.at("width").get<int>();
  input.camera.height = j.at("height").get<int>();
  PairMatches pm{9, 10, readInliers(dir / "llamav2_par_009_010.csv")};
  PairCandidate pc;
  pc.i = 9;
  pc.j = 10;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) pc.E(r, c) = j.at("E").at(r).at(c).get<double>();
  for (const InlierCorrespondence& c : pm.inliers) {
    pc.pts_i.emplace_back(c.point_i.x, c.point_i.y);
    pc.pts_j.emplace_back(c.point_j.x, c.point_j.y);
  }
  input.images = {9, 10};
  input.pairs = {pc};
  input.tracks = buildTracks({pm});
  return input;
}

}  // namespace

TEST(FocalSearch, ConfigDefaultsAndValidation) {
  const ReconstructionParams p;
  EXPECT_TRUE(p.init_focal_search);
  EXPECT_DOUBLE_EQ(p.init_focal_factor_min, 0.8);
  EXPECT_DOUBLE_EQ(p.init_focal_factor_max, 1.4);
  EXPECT_DOUBLE_EQ(p.init_focal_factor_step, 0.05);
  EXPECT_DOUBLE_EQ(p.max_reproj_error_px, 2.0);  // el umbral no cambia
  EXPECT_THROW(applyReconstructionJson(p, nlohmann::json{{"init_focal_factor_step", 0}}), std::invalid_argument);
  EXPECT_THROW(applyReconstructionJson(p, nlohmann::json{{"init_focal_factor_min", 1.5}}), std::invalid_argument);
  EXPECT_THROW(applyReconstructionJson(p, nlohmann::json{{"init_focal_search", 1}}), std::invalid_argument);
}

TEST(FocalSearch, RealPairWithWrongFocalFailsWithoutSearch) {
  const SfmResult r = runIncrementalSfm(llamav2Pair(), withSearch(false));
  ASSERT_TRUE(r.init.found);
  EXPECT_EQ(r.init_points, 0);  // con la focal del EXIF (2888,8 px) ningún punto pasa los 2 px
  EXPECT_TRUE(r.focal_trials.empty());
}

TEST(FocalSearch, GridRecoversTheRealPair) {
  const SfmInput input = llamav2Pair();
  const SfmResult r = runIncrementalSfm(input, withSearch(true));
  ASSERT_TRUE(r.init.found);
  EXPECT_EQ(r.focal_trials.size(), 13u);  // 0,80 a 1,40 cada 0,05
  EXPECT_GT(r.init_points, 300);
  // La focal elegida queda entre +10 % y +20 % (el BA de la sesión completa converge a +11,9 %).
  EXPECT_GE(r.init_focal_factor, 1.10 - 1e-9);
  EXPECT_LE(r.init_focal_factor, 1.20 + 1e-9);
  for (const FocalTrial& t : r.focal_trials) EXPECT_LE(t.points, r.init_points);  // el elegido es el mejor
  ASSERT_FALSE(r.ba_runs.empty());
  EXPECT_GT(r.ba_runs.front().n_observations, 600);  // el primer BA trabaja con los puntos del par
  EXPECT_LT(r.ba_runs.front().rms_after_px, 1.0);
  // El BA sigue refinando f a partir de la focal elegida (no queda fija en la rejilla).
  EXPECT_NE(r.rec.camera.f, input.camera.f * r.init_focal_factor);
}

TEST(FocalSearch, NarrowGridIsRespected) {
  ReconstructionParams p = withSearch(true);
  p.init_focal_factor_min = 0.9;
  p.init_focal_factor_max = 1.0;  // rejilla que no llega a la focal buena
  const SfmResult r = runIncrementalSfm(llamav2Pair(), p);
  EXPECT_EQ(r.focal_trials.size(), 3u);
  EXPECT_EQ(r.init_points, 0);
  EXPECT_DOUBLE_EQ(r.init_focal_factor, 1.0);
}

TEST(FocalSearch, CorrectFocalSkipsTheGridAndGivesIdenticalResult) {
  scene::RingOptions o;
  o.phone_camera = true;
  const scene::SfmScene s = scene::makeRingScene(o);
  ReconstructionParams on, off;
  off.init_focal_search = false;
  const SfmResult a = runIncrementalSfm(s.input, on);
  const SfmResult b = runIncrementalSfm(s.input, off);
  EXPECT_DOUBLE_EQ(a.init_focal_factor, 1.0);
  EXPECT_TRUE(a.focal_trials.empty());
  EXPECT_GT(a.init_points, 0);
  EXPECT_EQ(a.init_points, b.init_points);
  EXPECT_EQ(a.rec.registeredCount(), b.rec.registeredCount());
  EXPECT_EQ(a.rec.points.size(), b.rec.points.size());
  // Ceres con varios hilos cambia el orden de las sumas: diferencias de ~1e-14.
  EXPECT_NEAR(a.errors_after.global.rms, b.errors_after.global.rms, 1e-9);
  EXPECT_NEAR(a.rec.camera.f, b.rec.camera.f, 1e-6);
}
