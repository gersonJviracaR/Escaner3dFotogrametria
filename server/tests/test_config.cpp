// Pruebas de la configuración (umbrales sobreescribibles con --config).
#include "config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

TEST(Config, DefaultsMatchSprintSpec) {
  const ServerConfig c;
  EXPECT_EQ(c.port, 8080);
  EXPECT_EQ(c.max_photo_bytes, 64u * 1024u * 1024u);
  EXPECT_DOUBLE_EQ(c.quality.sharpness_min, 94.0);  // calibrado con fotos reales (docs/sprint1-reporte.md)
  EXPECT_DOUBLE_EQ(c.quality.overexposed_max, 0.15);
}

TEST(Config, JsonOverridesOnlyPresentFields) {
  const ServerConfig c = applyConfigJson(ServerConfig{}, json::parse(R"({"quality":{"sharpness_min":60}})"));
  EXPECT_DOUBLE_EQ(c.quality.sharpness_min, 60.0);
  EXPECT_DOUBLE_EQ(c.quality.overexposed_max, 0.15);
  EXPECT_EQ(c.port, 8080);
}

TEST(Config, InvalidJsonValuesThrow) {
  EXPECT_THROW(applyConfigJson(ServerConfig{}, json::parse(R"({"port":"x"})")), ConfigError);
  EXPECT_THROW(applyConfigJson(ServerConfig{}, json::parse(R"({"port":70000})")), ConfigError);
  EXPECT_THROW(applyConfigJson(ServerConfig{}, json::array()), ConfigError);
}

TEST(Config, CommandLinePortWinsOverConfigFile) {
  const fs::path file = fs::temp_directory_path() / "escaner3d_test_config.json";
  {
    std::ofstream out(file);
    out << R"({"port": 9000, "data_dir": "otra", "quality": {"overexposed_max": 0.3}})";
  }
  const ServerConfig c = applyCommandLine(ServerConfig{}, {"--port", "8081", "--config", file.string()});
  EXPECT_EQ(c.port, 8081);
  EXPECT_EQ(c.data_dir, "otra");
  EXPECT_DOUBLE_EQ(c.quality.overexposed_max, 0.3);
  fs::remove(file);
}

TEST(MatchingConfig, DefaultsMatchSprint2Spec) {
  const MatchingParams p = ServerConfig{}.matching;
  EXPECT_EQ(p.work_long_side, 2000);
  EXPECT_EQ(p.max_features, 8000);
  EXPECT_TRUE(p.clahe);
  EXPECT_DOUBLE_EQ(p.clahe_clip_limit, 2.0);
  EXPECT_EQ(p.clahe_tile, 8);
  EXPECT_DOUBLE_EQ(p.ratio, 0.75);
  EXPECT_DOUBLE_EQ(p.max_angle_deg, 45.0);
  EXPECT_EQ(p.max_neighbors, 6);
  EXPECT_DOUBLE_EQ(p.ransac_threshold_px, 1.0);
  EXPECT_DOUBLE_EQ(p.ransac_confidence, 0.999);
  EXPECT_EQ(p.min_inliers, 30);
  EXPECT_DOUBLE_EQ(p.min_inlier_ratio, 0.25);
  EXPECT_DOUBLE_EQ(p.fallback_focal_factor, 1.2);
}

TEST(MatchingConfig, PartialOverrideKeepsOtherValues) {
  const ServerConfig c =
      applyConfigJson(ServerConfig{}, json::parse(R"({"matching":{"ratio":0.8,"clahe":false}})"));
  EXPECT_DOUBLE_EQ(c.matching.ratio, 0.8);
  EXPECT_FALSE(c.matching.clahe);
  EXPECT_EQ(c.matching.max_features, 8000);
  EXPECT_DOUBLE_EQ(c.quality.sharpness_min, 94.0);
}

TEST(MatchingConfig, InvalidValuesThrow) {
  auto apply = [](const char* text) { return applyConfigJson(ServerConfig{}, json::parse(text)); };
  EXPECT_THROW(apply(R"({"matching":{"ratio":1.5}})"), ConfigError);
  EXPECT_THROW(apply(R"({"matching":{"ratio":0}})"), ConfigError);
  EXPECT_THROW(apply(R"({"matching":{"max_features":0}})"), ConfigError);
  EXPECT_THROW(apply(R"({"matching":{"ransac_confidence":1.0}})"), ConfigError);
  EXPECT_THROW(apply(R"({"matching":{"min_inlier_ratio":-0.1}})"), ConfigError);
  EXPECT_THROW(apply(R"({"matching":{"clahe":"si"}})"), ConfigError);
  EXPECT_THROW(apply(R"({"matching":{"ratoi":0.7}})"), ConfigError);  // clave desconocida
  EXPECT_THROW(apply(R"({"matching":[]})"), ConfigError);
}

TEST(MatchingConfig, DefaultConfigFileMatchesCodeDefaults) {
  const ServerConfig c = applyConfigFile(ServerConfig{}, ESCANER3D_DEFAULT_CONFIG);
  EXPECT_EQ(toJson(c.matching), toJson(MatchingParams{}));
}

TEST(MatchingConfig, CommandLineOverrides) {
  MatchingParams p = applyMatchingOverride(MatchingParams{}, "--max-angle-deg", "30");
  p = applyMatchingOverride(p, "--clahe", "false");
  p = applyMatchingOverride(p, "--min-inliers", "50");
  EXPECT_DOUBLE_EQ(p.max_angle_deg, 30.0);
  EXPECT_FALSE(p.clahe);
  EXPECT_EQ(p.min_inliers, 50);
  EXPECT_THROW(applyMatchingOverride(p, "--ratio", "abc"), std::invalid_argument);
  EXPECT_THROW(applyMatchingOverride(p, "--no-existe", "1"), std::invalid_argument);
  EXPECT_THROW(applyMatchingOverride(p, "--ratio", "2"), std::invalid_argument);
  EXPECT_TRUE(isMatchingOverride("--work-long-side"));
  EXPECT_FALSE(isMatchingOverride("--session"));
}

TEST(Config, CommandLineErrors) {
  EXPECT_THROW(applyCommandLine(ServerConfig{}, {"--port"}), ConfigError);
  EXPECT_THROW(applyCommandLine(ServerConfig{}, {"--port", "abc"}), ConfigError);
  EXPECT_THROW(applyCommandLine(ServerConfig{}, {"--desconocida"}), ConfigError);
  EXPECT_THROW(applyCommandLine(ServerConfig{}, {"--config", "no_existe.json"}), ConfigError);
}
