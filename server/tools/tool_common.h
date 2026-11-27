// Utilidades comunes de las herramientas de línea de comandos del Sprint 4.
#pragma once

#include "config.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <filesystem>
#include <string>

namespace tool {

inline std::filesystem::path executableDir(const char* argv0) {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return std::filesystem::path(buffer).parent_path();
#endif
  return std::filesystem::absolute(argv0).parent_path();
}

inline void utf8Console() {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
}

// config.default.json junto al ejecutable y luego cada --config de la línea de comandos.
inline ServerConfig loadConfig(const char* argv0, const std::vector<std::string>& args) {
  ServerConfig config;
  const std::filesystem::path defaults = executableDir(argv0) / "config.default.json";
  if (std::filesystem::exists(defaults)) config = applyConfigFile(config, defaults);
  for (std::size_t i = 0; i + 1 < args.size(); ++i)
    if (args[i] == "--config") config = applyConfigFile(config, args[i + 1]);
  return config;
}

inline std::string seconds(double ms) {
  char b[32];
  std::snprintf(b, sizeof(b), "%.1f s", ms / 1000.0);
  return b;
}

}  // namespace tool
