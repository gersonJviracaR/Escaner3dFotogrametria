// scanner_reconstruct: reconstrucción dispersa con bundle adjustment de una sesión ya
// emparejada por scanner_match (Sprint 3, Epic 3).
//
// Uso: scanner_reconstruct --session <carpeta> [--config archivo.json] [--include-rejected]
//                          [--debug] [--<parámetro> valor ...]   p. ej. --max-reproj-error-px 3
#include "config.h"
#include "session_reconstruction.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* const kUsage =
    "Uso: scanner_reconstruct --session <carpeta> [--config archivo.json] [--include-rejected] [--debug]\n"
    "                         [--<parámetro> valor ...]\n"
    "Parámetros (sección \"reconstruction\"): --init-min-inliers, --init-min-median-triangulation-angle-deg,\n"
    "  --tri-min-angle-deg, --max-reproj-error-px, --pnp-ransac-threshold-px, --pnp-confidence,\n"
    "  --pnp-min-inliers, --min-views, --ba-every, --ba-max-iterations, --huber-delta-px,\n"
    "  --refine-focal, --refine-distortion, --exclude-rejected-photos\n";

fs::path executableDir(const char* argv0) {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return fs::path(buffer).parent_path();
#endif
  return fs::absolute(argv0).parent_path();
}

std::string fmt(const char* f, double a, double b = 0, double c = 0, double d = 0) {
  char buffer[160];
  std::snprintf(buffer, sizeof(buffer), f, a, b, c, d);
  return buffer;
}

void printReport(const ReconstructionReport& r) {
  std::cout << "\n  foto  estado        obs.   medio(antes)  RMS(antes)  medio(desp.)  RMS(desp.)  máx(desp.)\n";
  for (const CameraSummary& c : r.cameras) {
    if (!c.registered) {
      std::cout << fmt("  %4.0f", c.index) << "  NO REGISTRADA (" << c.reason << ")\n";
      continue;
    }
    std::cout << fmt("  %4.0f", c.index) << "  registrada  " << fmt("%6.0f", c.after.n)
              << fmt("   %10.3f  %10.3f", c.before.mean, c.before.rms)
              << fmt("  %12.3f  %10.3f  %10.3f", c.after.mean, c.after.rms, c.after.max) << "\n";
  }
  int registered = 0;
  for (const CameraSummary& c : r.cameras) registered += c.registered ? 1 : 0;

  std::cout << "\nResumen de la sesión " << r.session_id << "\n";
  std::cout << "  par inicial: " << r.init_i << "-" << r.init_j << "\n";
  std::cout << "  cámaras registradas: " << registered << " de " << r.cameras.size() << " candidatas\n";
  for (const auto& [index, reason] : r.excluded_photos)
    std::cout << "    foto " << index << " excluida: " << reason << "\n";
  std::cout << "  pistas: " << r.tracks << " (" << r.tracks_discarded_inconsistent << " descartadas por inconsistentes)\n";
  std::cout << "  puntos: " << r.points_before_filter << " antes del filtrado, " << r.points_after_filter
            << " al final\n";
  std::cout << "  longitud de pista (mín / mediana / media / máx): "
            << fmt("%.0f / %.0f / %.2f / %.0f", r.track_length.min, r.track_length.median, r.track_length.mean,
                   r.track_length.max)
            << "\n";
  std::cout << "  error global antes del BA final: medio " << fmt("%.3f", r.global_before.mean) << " px, RMS "
            << fmt("%.3f", r.global_before.rms) << " px, máx " << fmt("%.2f", r.global_before.max) << " px\n";
  std::cout << "  error global final:              medio " << fmt("%.3f", r.global_after.mean) << " px, RMS "
            << fmt("%.3f", r.global_after.rms) << " px, máx " << fmt("%.2f", r.global_after.max) << " px\n";
  std::cout << "  intrínsecos: f " << fmt("%.1f -> %.1f px", r.initial_intrinsics.f, r.final_intrinsics.f) << ", k1 "
            << fmt("%.4f", r.final_intrinsics.k1) << ", k2 " << fmt("%.4f", r.final_intrinsics.k2) << "\n";
  std::cout << "  resolvedor: " << r.solver << ", corridas de BA: " << r.ba_runs.size() << "\n";
  if (r.imu_consistency.contains("mean_abs_diff_deg"))
    std::cout << "  IMU vs reconstrucción (cámaras consecutivas): diferencia media "
              << fmt("%.2f", r.imu_consistency.at("mean_abs_diff_deg").get<double>()) << "°, máx "
              << fmt("%.2f", r.imu_consistency.at("max_abs_diff_deg").get<double>()) << "°\n";
  if (r.ring_geometry.contains("mean_distance"))
    std::cout << "  centros al centroide: media " << fmt("%.3f", r.ring_geometry.at("mean_distance").get<double>())
              << ", desviación " << fmt("%.3f", r.ring_geometry.at("std_distance").get<double>())
              << " (escala: 1 = distancia del par inicial)\n";
  std::cout << "  tiempos (s):";
  for (const auto& [stage, ms] : r.timing_ms) std::cout << " " << stage << " " << fmt("%.1f", ms / 1000);
  std::cout << "\n";
  for (const std::string& w : r.warnings) std::cout << "  aviso: " << w << "\n";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  const std::vector<std::string> args(argv + 1, argv + argc);
  fs::path session;
  SessionReconstructionOptions options;
  ServerConfig config;
  try {
    const fs::path defaults = executableDir(argv[0]) / "config.default.json";
    if (fs::exists(defaults)) config = applyConfigFile(config, defaults);
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
      if (args[i] == "--config") config = applyConfigFile(config, args[i + 1]);
    for (std::size_t i = 0; i < args.size(); ++i) {
      const std::string& a = args[i];
      const bool has_value = i + 1 < args.size();
      if (a == "--help" || a == "-h") {
        std::cout << kUsage;
        return 0;
      } else if (a == "--debug") {
        options.debug = true;
      } else if (a == "--include-rejected") {
        options.include_rejected = true;
      } else if (a == "--session" && has_value) {
        session = args[++i];
      } else if (a == "--config" && has_value) {
        ++i;
      } else if (isReconstructionOverride(a) && has_value) {
        config.reconstruction = applyReconstructionOverride(config.reconstruction, a, args[++i]);
      } else {
        throw std::invalid_argument("opción desconocida o sin valor: " + a);
      }
    }
    if (session.empty()) throw std::invalid_argument("falta --session");
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n" << kUsage;
    return 2;
  }

  options.log = [](const std::string& text) { std::cout << "  " << text << "\n" << std::flush; };
  std::cout << "Escáner 3D - reconstrucción dispersa (Sprint 3)\nSesión: " << fs::absolute(session).string() << "\n"
            << "Parámetros: " << toJson(config.reconstruction).dump() << "\n";
  try {
    const SessionReconstructionResult result = runSessionReconstruction(session, config.reconstruction, options);
    printReport(result.report);
    std::cout << "\nSalidas en: " << (session / "reconstruction").string() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
