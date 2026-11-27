// scanner_texture: textura por triángulo (HU-21) y exportación OBJ/MTL/PNG con escala
// opcional (HU-22) de la malla que dejó scanner_dense (Sprint 4, Epic 4).
//
// Uso: scanner_texture --session <carpeta> [--config archivo.json] [--poses ours|colmap]
//                      [--scale-ref "x1,y1,z1;x2,y2,z2;D_mm"] [--scale s]
#include "obj_export.h"
#include "session_pipeline.h"
#include "texturing.h"
#include "tool_common.h"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const char* const kUsage =
    "Uso: scanner_texture --session <carpeta> [--config archivo.json] [--poses ours|colmap]\n"
    "                     [--scale-ref \"x1,y1,z1;x2,y2,z2;D_mm\"] [--scale s]\n"
    "  --scale-ref  dos puntos del modelo (p. ej. tomados en MeshLab) y su distancia real en mm\n"
    "  --scale      factor directo; sin ninguno de los dos, escala arbitraria\n";

}  // namespace

int main(int argc, char** argv) {
  tool::utf8Console();
  const std::vector<std::string> args(argv + 1, argv + argc);
  fs::path session;
  std::string poses = "ours";
  ScaleSpec scale;
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
      } else if (a == "--poses" && has_value) {
        poses = args[++i];
      } else if (a == "--scale-ref" && has_value) {
        scale = parseScaleRef(args[++i]);
      } else if (a == "--scale" && has_value) {
        scale = scaleByFactor(std::stod(args[++i]));
      } else {
        throw std::invalid_argument("opción desconocida o sin valor: " + a);
      }
    }
    if (session.empty()) throw std::invalid_argument("falta --session");
    sessionPaths(session, poses);
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n" << kUsage;
    return 2;
  }

  try {
    const auto t0 = std::chrono::steady_clock::now();
    const SessionPaths paths = sessionPaths(session, poses);
    const fs::path mesh_file = paths.model / "mesh.ply";
    if (!fs::exists(mesh_file)) throw std::runtime_error("falta " + mesh_file.string() + ": ejecuta antes scanner_dense");
    const Mesh mesh = readPlyMesh(mesh_file);
    const Reconstruction rec = loadPoses(session, poses);
    std::map<int, Pose> cams;
    for (const auto& [index, cam] : rec.cameras)
      if (cam.registered) cams[index] = cam.pose;
    const std::map<int, std::string> names = photoNames(session);
    std::cout << "Escáner 3D - textura y exportación (Sprint 4)\nSesión: " << fs::absolute(session).string()
              << "\nPoses: " << poses << ", " << cams.size() << " cámaras; malla: " << mesh.vertices.size()
              << " vértices, " << mesh.faces.size() << " caras\n" << std::flush;

    // HU-21
    const auto t_tex = std::chrono::steady_clock::now();
    const TextureResult tex = textureMesh(
        mesh, rec.camera, cams,
        [&](int index) {
          const cv::Mat img = cv::imread((session / names.at(index)).string(), cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
          if (img.empty()) throw std::runtime_error("no se pudo leer la foto " + names.at(index));
          return img;
        },
        config.texture);
    const double tex_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_tex).count();
    json per_camera = json::object();
    for (const auto& [index, n] : tex.stats.faces_per_camera) per_camera[std::to_string(index)] = n;
    updateModelReport(paths.model, "texture",
                      json{{"params", toJson(config.texture)},
                           {"faces", tex.stats.faces},
                           {"textured_faces", tex.stats.textured_faces},
                           {"textured_pct", tex.stats.textured_pct},
                           {"faces_per_camera", per_camera},
                           {"charts", tex.stats.charts},
                           {"atlas_width", tex.stats.atlas_width},
                           {"atlas_height", tex.stats.atlas_height},
                           {"chart_scale", tex.stats.chart_scale},
                           {"time_ms", tex_ms}});
    std::cout << "  textura: " << tex.stats.textured_faces << " de " << tex.stats.faces << " caras ("
              << json(tex.stats.textured_pct).dump() << " %), " << tex.stats.charts << " cartas, atlas "
              << tex.stats.atlas_width << "x" << tex.stats.atlas_height << ", escala de cartas "
              << tex.stats.chart_scale << " (" << tool::seconds(tex_ms) << ")\n  caras por cámara:";
    for (const auto& [index, n] : tex.stats.faces_per_camera) std::cout << " " << index << ":" << n;
    std::cout << "\n";

    // HU-22
    const ObjExport ex = exportObj(paths.model, mesh, tex, scale);
    updateModelReport(paths.model, "export",
                      json{{"obj", (paths.model / "model.obj").string()},
                           {"scale", ex.scale},
                           {"unit", ex.unit},
                           {"vertices", ex.vertices},
                           {"faces", ex.faces},
                           {"closed", ex.closed},
                           {"open_edges", ex.open_edges}});
    std::cout << "  OBJ: " << (paths.model / "model.obj").string() << " (+ model.mtl, texture.png)\n  escala: "
              << ex.scale << " (" << ex.unit << "); malla " << (ex.closed ? "cerrada" : "abierta") << ", "
              << ex.open_edges << " aristas abiertas\n";
    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "Total " << tool::seconds(total) << "\n";
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
