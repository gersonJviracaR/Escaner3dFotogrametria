// scanner_accuracy: reporte de precisión de la reconstrucción propia frente a la de COLMAP
// sobre las mismas fotos aceptadas (HU-24; Sprint 4, Epic 4).
//
// Uso: scanner_accuracy --session <carpeta> [--config archivo.json] [--colmap <ruta>] [--reuse-reference]
#include "accuracy.h"
#include "atomic_file.h"
#include "colmap_io.h"
#include "manifest.h"
#include "session_pipeline.h"
#include "tool_common.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const char* const kUsage =
    "Uso: scanner_accuracy --session <carpeta> [--config archivo.json] [--colmap <ruta>] [--reuse-reference]\n"
    "  --reuse-reference  usa colmap_ref\\sparse_txt si ya existe en vez de volver a generarla\n";

std::string f3(double v) {
  char b[32];
  std::snprintf(b, sizeof(b), "%.4g", v);
  return b;
}

}  // namespace

int main(int argc, char** argv) {
  tool::utf8Console();
  const std::vector<std::string> args(argv + 1, argv + argc);
  fs::path session;
  bool reuse = false;
  ServerConfig config;
  try {
    config = tool::loadConfig(argv[0], args);
    for (std::size_t i = 0; i < args.size(); ++i) {
      const std::string& a = args[i];
      const bool has_value = i + 1 < args.size();
      if (a == "--help" || a == "-h") {
        std::cout << kUsage;
        return 0;
      } else if (a == "--session" && has_value) {
        session = args[++i];
      } else if (a == "--config" && has_value) {
        ++i;
      } else if (a == "--colmap" && has_value) {
        config.dense.colmap_path = args[++i];
      } else if (a == "--reuse-reference") {
        reuse = true;
      } else {
        throw std::invalid_argument("opción desconocida o sin valor: " + a);
      }
    }
    if (session.empty()) throw std::invalid_argument("falta --session");
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n" << kUsage;
    return 2;
  }

  try {
    const auto t0 = std::chrono::steady_clock::now();
    const fs::path ref = session / "colmap_ref";
    const fs::path out = session / "accuracy";
    fs::create_directories(out);
    std::cout << "Escáner 3D - precisión frente a COLMAP (Sprint 4)\nSesión: " << fs::absolute(session).string() << "\n";
    double ref_ms = 0;
    if (!(reuse && fs::exists(ref / "sparse_txt" / "images.txt"))) {
      ColmapRunner colmap(findColmap(config.dense.colmap_path), out / "colmap.log");
      std::vector<std::string> accepted;
      for (const PhotoRecord& p : readManifest(session / "manifest.json").photos)
        if (p.quality.accepted) accepted.push_back(p.file);
      std::cout << "  referencia de COLMAP con " << accepted.size() << " fotos aceptadas (" << colmapVersion(colmap.help().output)
                << ")...\n" << std::flush;
      const auto tr = std::chrono::steady_clock::now();
      const int n = generateColmapReference(colmap, session, accepted, ref);
      ref_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tr).count();
      std::cout << "  COLMAP registró " << n << " imágenes (" << tool::seconds(ref_ms) << ")\n";
    }
    const Reconstruction ours = loadPoses(session, "ours");
    const Reconstruction colmap_rec = loadPoses(session, "colmap");
    const AccuracyReport r = compareReconstructions(ours, colmap_rec, config.accuracy);
    json j = toJson(r);
    j["params"] = toJson(config.accuracy);
    j["reference_ms"] = ref_ms;
    j["colmap_reference"] = (ref / "sparse_txt").string();
    writeFileAtomic(out / "accuracy_report.json", j.dump(2) + '\n');

    std::cout << "\n  cámaras: propias " << r.ours_registered << ", COLMAP " << r.colmap_registered << ", comunes "
              << r.common << (r.reliable ? "" : " (NO FIABLE)") << "\n  " << r.note << "\n";
    if (r.reliable) {
      std::cout << "  escala propia -> COLMAP: " << f3(r.scale) << "; radio medio del anillo (COLMAP): " << f3(r.ring_radius)
                << "\n  error de posición: medio " << f3(r.position_mean) << ", RMS " << f3(r.position_rms) << ", máx "
                << f3(r.position_max) << " (" << f3(100 * r.position_mean / r.ring_radius) << " % / "
                << f3(100 * r.position_rms / r.ring_radius) << " % / " << f3(100 * r.position_max / r.ring_radius)
                << " % del radio)\n  error de rotación: medio " << f3(r.rotation_mean_deg) << "°, máx "
                << f3(r.rotation_max_deg) << "°\n";
      std::cout << "  por cámara:";
      for (const CameraAccuracy& c : r.cameras)
        std::cout << " " << c.index << "(" << f3(c.position_error) << ", " << f3(c.rotation_error_deg) << "°"
                  << (c.inlier ? "" : ", atípica") << ")";
      std::cout << "\n  puntos dispersos propios -> COLMAP más cercano: media " << f3(r.point_distance_mean) << ", mediana "
                << f3(r.point_distance_median) << "\n";
    }
    std::cout << "  f: propia " << f3(r.f_ours) << " px, COLMAP " << f3(r.f_colmap) << " px ("
              << f3(100 * (r.f_ours - r.f_colmap) / r.f_colmap) << " %)\n  k1, k2: propios " << f3(r.k1_ours) << ", "
              << f3(r.k2_ours) << "; COLMAP " << f3(r.k1_colmap) << ", " << f3(r.k2_colmap)
              << "\n  error de reproyección: propio medio " << f3(r.reproj_mean_ours) << " px, RMS " << f3(r.reproj_rms_ours)
              << " px; COLMAP medio " << f3(r.reproj_mean_colmap) << " px, RMS " << f3(r.reproj_rms_colmap) << " px\n"
              << "  puntos dispersos: propios " << r.points_ours << ", COLMAP " << r.points_colmap << "\n";
    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "Reporte: " << (out / "accuracy_report.json").string() << " (total " << tool::seconds(total) << ")\n";
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
