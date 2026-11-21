// scanner_match: características SIFT, pares vecinos y geometría epipolar de una sesión
// ya almacenada (Sprint 2, Epic 2).
//
// Uso: scanner_match --session <carpeta> [--config archivo.json] [--debug]
//                    [--<parámetro> valor ...]   p. ej. --max-angle-deg 30 --clahe false
#include "config.h"
#include "session_matching.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* const kUsage =
    "Uso: scanner_match --session <carpeta> [--config archivo.json] [--debug] [--<parámetro> valor ...]\n"
    "Parámetros (sección \"matching\"): --work-long-side, --max-features, --clahe, --clahe-clip-limit,\n"
    "  --clahe-tile, --ratio, --max-angle-deg, --max-neighbors, --ransac-threshold-px,\n"
    "  --ransac-confidence, --min-inliers, --min-inlier-ratio, --fallback-focal-factor\n";

fs::path executableDir(const char* argv0) {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return fs::path(buffer).parent_path();
#endif
  return fs::absolute(argv0).parent_path();
}

struct Stats {
  double min = 0, median = 0, max = 0;
};

Stats stats(std::vector<double> v) {
  if (v.empty()) return {};
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  const double median = n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
  return {v.front(), median, v.back()};
}

std::string fmt(const char* f, double a, double b = 0, double c = 0) {
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), f, a, b, c);
  return buffer;
}

void printReport(const MatchingReport& r) {
  std::cout << "\n  par      ángulo  corresp.  inliers   razón  Sampson  estado\n";
  for (const PairReport& p : r.pairs) {
    std::string reasons;
    for (const std::string& s : p.reasons) reasons += (reasons.empty() ? "" : ",") + s;
    std::cout << fmt("  %3.0f-%-3.0f", p.i, p.j) << fmt("  %6.1f", p.angle_deg) << fmt("  %8.0f", p.n_matches)
              << fmt("  %7.0f", p.n_inliers) << fmt("  %6.3f", p.inlier_ratio) << fmt("  %7.3f", p.mean_sampson_px)
              << "  " << (p.accepted ? "aceptado" : "RECHAZADO (" + reasons + ")") << "\n";
  }

  const MatchingSummary s = summarize(r);
  std::vector<double> feats, extract, load, match, inliers, ratio, sampson, n_matches;
  for (const ViewReport& v : r.views) {
    feats.push_back(v.n_features);
    extract.push_back(v.extract_ms);
    load.push_back(v.load_ms);
  }
  std::map<std::string, int> reasons;
  for (const PairReport& p : r.pairs) {
    match.push_back(p.match_ms + p.geometry_ms);
    if (p.accepted) {
      n_matches.push_back(p.n_matches);
      inliers.push_back(p.n_inliers);
      ratio.push_back(p.inlier_ratio);
      sampson.push_back(p.mean_sampson_px);
    }
    for (const std::string& reason : p.reasons) ++reasons[reason];
  }
  auto line = [](const char* label, const Stats& st, const char* f) {
    std::cout << "  " << label << fmt(f, st.min, st.median, st.max) << "\n";
  };

  std::cout << "\nResumen de la sesión " << r.session_id << "\n";
  std::cout << "  vistas usadas: " << s.views_used << " (excluidas: " << s.views_excluded << ")\n";
  for (const ExcludedView& e : r.excluded_views) std::cout << "    foto " << e.index << ": " << e.reason << "\n";
  std::cout << "  pares seleccionados: " << s.pairs_selected << " de " << s.pairs_all_vs_all
            << " (todos contra todos)\n";
  std::cout << "  aceptados: " << s.pairs_accepted << ", rechazados: " << s.pairs_rejected << "\n";
  for (const auto& [reason, count] : reasons) std::cout << "    " << reason << ": " << count << "\n";
  line("puntos por foto         (mín / mediana / máx): ", stats(feats), "%.0f / %.0f / %.0f");
  line("corresp. pares aceptados (mín / mediana / máx): ", stats(n_matches), "%.0f / %.0f / %.0f");
  line("inliers pares aceptados  (mín / mediana / máx): ", stats(inliers), "%.0f / %.0f / %.0f");
  line("razón de inliers         (mín / mediana / máx): ", stats(ratio), "%.3f / %.3f / %.3f");
  line("Sampson medio, px        (mín / mediana / máx): ", stats(sampson), "%.3f / %.3f / %.3f");
  line("lectura JPEG por foto, ms(mín / mediana / máx): ", stats(load), "%.0f / %.0f / %.0f");
  line("extracción por foto, ms  (mín / mediana / máx): ", stats(extract), "%.0f / %.0f / %.0f");
  line("emparejamiento+F por par, ms (mín/med/máx):     ", stats(match), "%.0f / %.0f / %.0f");
  std::cout << "  tiempo total: extracción " << fmt("%.1f s", r.extraction_ms / 1000) << ", pares "
            << fmt("%.1f s", r.matching_ms / 1000) << ", total " << fmt("%.1f s", r.total_ms / 1000) << "\n";
  for (const std::string& w : r.warnings) std::cout << "  aviso: " << w << "\n";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  const std::vector<std::string> args(argv + 1, argv + argc);
  fs::path session;
  SessionMatchingOptions options;
  ServerConfig config;
  try {
    const fs::path defaults = executableDir(argv[0]) / "config.default.json";
    if (fs::exists(defaults)) config = applyConfigFile(config, defaults);
    // Primero el archivo --config, para que las opciones sueltas lo sobrescriban.
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
      } else if (a == "--session" && has_value) {
        session = args[++i];
      } else if (a == "--config" && has_value) {
        ++i;
      } else if (isMatchingOverride(a) && has_value) {
        config.matching = applyMatchingOverride(config.matching, a, args[++i]);
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
  std::cout << "Escáner 3D - emparejamiento (Sprint 2)\nSesión: " << fs::absolute(session).string() << "\n"
            << "Parámetros: " << toJson(config.matching).dump() << "\n";
  try {
    const MatchingReport report = runSessionMatching(session, config.matching, options);
    printReport(report);
    std::cout << "\nReporte: " << (session / "matching_report.json").string() << "\n";
    if (options.debug) std::cout << "Imágenes de depuración en: " << (session / "debug").string() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
