// scanner_dense: densificación con COLMAP (HU-19) y malla (HU-20) de una sesión ya
// reconstruida por scanner_reconstruct (Sprint 4, Epic 4).
//
// Uso: scanner_dense --session <carpeta> [--config archivo.json] [--colmap <ruta>]
//                    [--max-image-size 1600] [--poses ours|colmap] [--skip-dense]
#include "config.h"
#include "dense.h"
#include "meshing.h"
#include "session_pipeline.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const char* const kUsage =
    "Uso: scanner_dense --session <carpeta> [--config archivo.json] [--colmap <ruta a colmap>]\n"
    "                   [--max-image-size N] [--poses ours|colmap] [--skip-dense]\n"
    "  --poses ours    (por defecto) poses refinadas del Sprint 3 (HU-19)\n"
    "  --poses colmap  poses de la referencia de COLMAP (comparación; requiere scanner_accuracy antes)\n"
    "  --skip-dense    reutiliza dense\\fused.ply y solo rehace la malla\n";

fs::path executableDir(const char* argv0) {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return fs::path(buffer).parent_path();
#endif
  return fs::absolute(argv0).parent_path();
}

std::string seconds(double ms) {
  char b[32];
  std::snprintf(b, sizeof(b), "%.1f s", ms / 1000.0);
  return b;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  const std::vector<std::string> args(argv + 1, argv + argc);
  fs::path session;
  std::string poses = "ours";
  bool skip_dense = false;
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
      } else if (a == "--session" && has_value) {
        session = args[++i];
      } else if (a == "--config" && has_value) {
        ++i;
      } else if (a == "--colmap" && has_value) {
        config.dense.colmap_path = args[++i];
      } else if (a == "--max-image-size" && has_value) {
        config.dense = applyDenseJson(config.dense, json{{"max_image_size", std::stoi(args[++i])}});
      } else if (a == "--poses" && has_value) {
        poses = args[++i];
      } else if (a == "--skip-dense") {
        skip_dense = true;
      } else {
        throw std::invalid_argument("opción desconocida o sin valor: " + a);
      }
    }
    if (session.empty()) throw std::invalid_argument("falta --session");
    sessionPaths(session, poses);  // valida --poses
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n" << kUsage;
    return 2;
  }

  try {
    const auto t0 = std::chrono::steady_clock::now();
    const SessionPaths paths = sessionPaths(session, poses);
    fs::create_directories(paths.model);
    ColmapRunner colmap(findColmap(config.dense.colmap_path), paths.model / "colmap.log");
    const std::string help = colmap.help().output;
    std::cout << "Escáner 3D - densificación y malla (Sprint 4)\nSesión: " << fs::absolute(session).string()
              << "\nPoses: " << poses << (poses == "ours" ? " (refinadas, Sprint 3)" : " (referencia de COLMAP)")
              << "\nCOLMAP: " << colmap.exe().string() << "\n  " << colmapVersion(help) << "\n";
    if (!colmapHasCuda(help)) {
      std::cerr << "Error: esta compilación de COLMAP no tiene CUDA; patch_match_stereo la necesita.\n";
      return 3;
    }

    // HU-19: modelo disperso y densificación.
    json dense_report{{"poses", poses}, {"params", toJson(config.dense)}, {"colmap", colmapVersion(help)}};
    int cameras = 0;
    if (poses == "ours") {
      cameras = exportOursToColmap(session, paths.sparse_model);
      std::cout << "  modelo propio exportado: " << cameras << " cámaras en " << paths.sparse_model.string() << "\n";
    } else {
      cameras = loadPoses(session, poses).registeredCount();
      std::cout << "  modelo de COLMAP: " << cameras << " cámaras en " << paths.sparse_model.string() << "\n";
    }
    dense_report["cameras"] = cameras;
    DenseResult dr;
    dr.fused_ply = paths.dense / "fused.ply";
    if (skip_dense) {
      if (!fs::exists(dr.fused_ply)) throw std::runtime_error("--skip-dense pero no existe " + dr.fused_ply.string());
      dr.dense_points = plyVertexCount(dr.fused_ply);
      std::cout << "  reutilizando " << dr.fused_ply.string() << "\n";
    } else {
      std::cout << "  densificando (image_undistorter, patch_match_stereo, stereo_fusion)...\n" << std::flush;
      dr = runDensification(colmap, session, paths.sparse_model, paths.dense, config.dense);
      dense_report["undistort_ms"] = dr.undistort_ms;
      dense_report["patch_match_ms"] = dr.patch_match_ms;
      dense_report["fusion_ms"] = dr.fusion_ms;
      std::cout << "  undistorter " << seconds(dr.undistort_ms) << ", patch_match " << seconds(dr.patch_match_ms)
                << ", fusion " << seconds(dr.fusion_ms) << "\n";
    }
    const int sparse_points = static_cast<int>(loadPoses(session, poses).points.size());
    dense_report["sparse_points"] = sparse_points;
    dense_report["dense_points"] = dr.dense_points;
    dense_report["fused_ply"] = dr.fused_ply.string();
    updateModelReport(paths.model, "dense", dense_report);
    std::cout << "  nube densa: " << dr.dense_points << " puntos (dispersa: " << sparse_points << ")\n";

    // HU-20: malla.
    const MeshSummary mesh = runMeshing(colmap, dr.fused_ply, paths.model, config.mesh);
    updateModelReport(paths.model, "mesh", toJson(mesh));
    std::cout << "  malla: " << mesh.vertices << " vértices, " << mesh.faces << " caras (de " << mesh.raw_faces
              << "), " << mesh.components << " componente(s), " << (mesh.closed ? "cerrada" : "abierta") << " ("
              << mesh.open_edges << " aristas abiertas), área " << mesh.area << "\n";
    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "Salidas en: " << paths.model.string() << " (total " << seconds(total) << ")\n";
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
