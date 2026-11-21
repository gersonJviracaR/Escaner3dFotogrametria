#include "config.h"

#include <fstream>

using nlohmann::json;

namespace {

int parsePort(const std::string& text) {
  std::size_t used = 0;
  int port = 0;
  try {
    port = std::stoi(text, &used);
  } catch (const std::exception&) {
    throw ConfigError("puerto inválido: " + text);
  }
  if (used != text.size() || port < 0 || port > 65535) throw ConfigError("puerto inválido: " + text);
  return port;
}

}  // namespace

ServerConfig applyConfigJson(ServerConfig c, const json& j) {
  if (!j.is_object()) throw ConfigError("la configuración debe ser un objeto JSON");
  try {
    if (j.contains("port")) c.port = parsePort(std::to_string(j.at("port").get<int>()));
    if (j.contains("data_dir")) c.data_dir = j.at("data_dir").get<std::string>();
    if (j.contains("max_photo_mb")) {
      const double mb = j.at("max_photo_mb").get<double>();
      if (mb <= 0) throw ConfigError("max_photo_mb debe ser positivo");
      c.max_photo_bytes = static_cast<std::size_t>(mb * 1024 * 1024);
    }
    if (j.contains("quality")) {
      const json& q = j.at("quality");
      if (q.contains("sharpness_min")) c.quality.sharpness_min = q.at("sharpness_min").get<double>();
      if (q.contains("overexposed_max")) c.quality.overexposed_max = q.at("overexposed_max").get<double>();
    }
    if (j.contains("matching")) c.matching = applyMatchingJson(c.matching, j.at("matching"));
    if (j.contains("reconstruction"))
      c.reconstruction = applyReconstructionJson(c.reconstruction, j.at("reconstruction"));
  } catch (const json::exception& e) {
    throw ConfigError(std::string("configuración inválida: ") + e.what());
  } catch (const std::invalid_argument& e) {
    throw ConfigError(std::string("configuración inválida: ") + e.what());
  }
  return c;
}

ServerConfig applyConfigFile(ServerConfig base, const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw ConfigError("no se pudo abrir " + file.string());
  try {
    return applyConfigJson(std::move(base), json::parse(in));
  } catch (const json::parse_error& e) {
    throw ConfigError("JSON inválido en " + file.string() + ": " + e.what());
  }
}

ServerConfig applyCommandLine(ServerConfig c, const std::vector<std::string>& args) {
  auto valueAfter = [&](std::size_t i) -> const std::string& {
    if (i + 1 >= args.size()) throw ConfigError("falta el valor de " + args[i]);
    return args[i + 1];
  };
  // Primero el archivo, para que --port y --data-dir lo sobrescriban.
  for (std::size_t i = 0; i < args.size(); ++i)
    if (args[i] == "--config") c = applyConfigFile(std::move(c), valueAfter(i));

  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "--config") {
      ++i;
    } else if (a == "--port") {
      c.port = parsePort(valueAfter(i));
      ++i;
    } else if (a == "--data-dir") {
      c.data_dir = valueAfter(i);
      ++i;
    } else {
      throw ConfigError("opción desconocida: " + a);
    }
  }
  return c;
}
