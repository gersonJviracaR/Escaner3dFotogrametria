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
  EXPECT_DOUBLE_EQ(c.quality.sharpness_min, 90.0);  // provisional, calibrado con fotos reales
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

TEST(Config, CommandLineErrors) {
  EXPECT_THROW(applyCommandLine(ServerConfig{}, {"--port"}), ConfigError);
  EXPECT_THROW(applyCommandLine(ServerConfig{}, {"--port", "abc"}), ConfigError);
  EXPECT_THROW(applyCommandLine(ServerConfig{}, {"--desconocida"}), ConfigError);
  EXPECT_THROW(applyCommandLine(ServerConfig{}, {"--config", "no_existe.json"}), ConfigError);
}
