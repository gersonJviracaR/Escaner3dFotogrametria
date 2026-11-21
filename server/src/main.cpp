// Servidor local del Escáner 3D: recibe las sesiones de fotos desde la app Android.
// Uso: scanner_server [--config archivo.json] [--port 8080] [--data-dir ruta]
#include "config.h"
#include "http_server.h"
#include "session_store.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path executableDir(const char* argv0) {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return fs::path(buffer).parent_path();
#endif
  return fs::absolute(argv0).parent_path();
}

// IPv4 locales de la PC (las que el usuario escribe en la app).
std::set<std::string> localIPv4Addresses() {
  std::set<std::string> ips;
  char host[256] = {};
  if (gethostname(host, sizeof(host)) != 0) return ips;
  addrinfo hints{};
  hints.ai_family = AF_INET;
  addrinfo* result = nullptr;
  if (getaddrinfo(host, nullptr, &hints, &result) != 0) return ips;
  for (addrinfo* p = result; p; p = p->ai_next) {
    char text[INET_ADDRSTRLEN] = {};
    const auto* addr = reinterpret_cast<const sockaddr_in*>(p->ai_addr);
    if (inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text)) && std::string(text).rfind("127.", 0) != 0)
      ips.insert(text);
  }
  freeaddrinfo(result);
  return ips;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

  ServerConfig config;
  try {
    const fs::path defaults = executableDir(argv[0]) / "config.default.json";
    if (fs::exists(defaults)) config = applyConfigFile(config, defaults);
    config = applyCommandLine(config, std::vector<std::string>(argv + 1, argv + argc));
  } catch (const ConfigError& e) {
    std::cerr << "Error de configuración: " << e.what() << "\n"
              << "Uso: scanner_server [--config archivo.json] [--port 8080] [--data-dir ruta]\n";
    return 2;
  }

  const fs::path dataDir = fs::absolute(config.data_dir);
  SessionStore store(dataDir, config.quality);
  ScannerHttpServer server(store, config.max_photo_bytes);

  std::cout << "Escáner 3D - servidor v" << kServerVersion << "\n"
            << "Sesiones en: " << dataDir.string() << "\n"
            << "Umbrales: nitidez >= " << config.quality.sharpness_min
            << ", saturación <= " << config.quality.overexposed_max << "\n"
            << "Escribe en la app una de estas direcciones:\n";
  const auto ips = localIPv4Addresses();
  if (ips.empty()) std::cout << "  (no se detectaron IPs locales; revisa la conexión de red)\n";
  for (const std::string& ip : ips) std::cout << "  " << ip << ":" << config.port << "\n";
  std::cout << "Ctrl+C para detener.\n" << std::flush;

  if (!server.listen("0.0.0.0", config.port)) {
    std::cerr << "No se pudo escuchar en el puerto " << config.port << " (¿está ocupado?)\n";
    return 1;
  }
  return 0;
}
