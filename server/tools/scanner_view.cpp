// scanner_view: visor de la nube dispersa, la nube densa y la malla texturizada en una
// vista general y una de detalle (HU-23; Sprint 4, Epic 4).
//
// Uso: scanner_view --session <carpeta> [--config archivo.json] [--poses ours|colmap]
//                   [--cloud sparse|dense] [--mesh] [--render salida.png]
// Ventana: arrastrar = rotar, rueda = zoom, clic = punto de detalle, 1/2/3 = nube dispersa /
// nube densa / malla, q o Esc = salir.
#include "meshing.h"
#include "obj_export.h"
#include "session_pipeline.h"
#include "tool_common.h"
#include "viewer.h"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>

#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* const kUsage =
    "Uso: scanner_view --session <carpeta> [--config archivo.json] [--poses ours|colmap]\n"
    "                  [--cloud sparse|dense] [--mesh] [--render salida.png]\n"
    "  Ventana: arrastrar = rotar, rueda = zoom, clic = punto de detalle,\n"
    "           1/2/3 = nube dispersa / nube densa / malla, q = salir\n";

struct Models {
  std::optional<RenderModel> sparse, dense, mesh;
};

Models loadModels(const fs::path& session, const std::string& poses) {
  Models m;
  const SessionPaths paths = sessionPaths(session, poses);
  const Reconstruction rec = loadPoses(session, poses);
  std::vector<cv::Vec3f> centers;
  for (const auto& [index, cam] : rec.cameras)
    if (cam.registered) centers.emplace_back(cam.pose.center());
  RenderModel sparse;
  for (const Point3D& p : rec.points) {
    sparse.points.emplace_back(p.xyz);
    sparse.point_colors.push_back(p.rgb);
  }
  sparse.camera_centers = centers;
  m.sparse = sparse;
  if (fs::exists(paths.dense / "fused.ply")) {
    const Mesh cloud = readPlyMesh(paths.dense / "fused.ply");
    RenderModel dense;
    dense.points = cloud.vertices;
    dense.point_colors = cloud.colors;
    dense.camera_centers = centers;
    m.dense = dense;
  }
  if (fs::exists(paths.model / "model.obj")) {
    const ObjModel obj = readObj(paths.model / "model.obj");
    RenderModel mesh;
    mesh.mesh = obj.mesh;
    mesh.face_uv = obj.face_uv;
    if (!obj.texture_file.empty()) mesh.texture = cv::imread((paths.model / obj.texture_file).string());
    mesh.camera_centers = centers;
    m.mesh = mesh;
  }
  return m;
}

// Punto del modelo cuya proyección cae más cerca de (x, y) en la vista general.
std::optional<cv::Vec3d> pick(const RenderModel& model, const ViewCamera& view, int w, int h, int x, int y) {
  const std::vector<cv::Vec3f>& pts = model.mesh.vertices.empty() ? model.points : model.mesh.vertices;
  double best = std::numeric_limits<double>::max();
  std::optional<cv::Vec3d> out;
  const Pose pose = orbitPose(view);
  for (const cv::Vec3f& p : pts) {
    if (pose.toCamera(cv::Vec3d(p))[2] <= 0) continue;
    const cv::Point2d u = projectView(view, w, h, cv::Vec3d(p));
    const double d = std::hypot(u.x - x, u.y - y);
    if (d < best) best = d, out = cv::Vec3d(p);
  }
  return best < 25 ? out : std::nullopt;
}

struct UiState {
  ViewCamera general, detail;
  bool dragging = false, moved = false, dirty = true, interactive = false;
  int last_x = 0, last_y = 0, down_x = 0, down_y = 0;
};

}  // namespace

int main(int argc, char** argv) {
  tool::utf8Console();
  const std::vector<std::string> args(argv + 1, argv + argc);
  fs::path session, render;
  std::string poses = "ours", cloud = "dense";
  bool want_mesh = false;
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
      } else if (a == "--cloud" && has_value) {
        cloud = args[++i];
        if (cloud != "sparse" && cloud != "dense") throw std::invalid_argument("--cloud debe ser sparse o dense");
      } else if (a == "--mesh") {
        want_mesh = true;
      } else if (a == "--render" && has_value) {
        render = args[++i];
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
    const Models models = loadModels(session, poses);
    int current = want_mesh ? 3 : (cloud == "sparse" ? 1 : 2);
    auto modelFor = [&](int which) -> const std::optional<RenderModel>& {
      return which == 1 ? models.sparse : which == 2 ? models.dense : models.mesh;
    };
    if (!modelFor(current)) throw std::runtime_error("no existe el modelo pedido (ejecuta antes scanner_dense / scanner_texture)");
    const int W = config.viewer.width, H = config.viewer.height, half = W / 2;

    UiState ui;
    auto resetViews = [&](const RenderModel& m) {
      ui.general = fitView(m);
      ui.general.yaw_deg = -60;
      ui.general.pitch_deg = 25;
      const std::optional<cv::Vec3d> center = pick(m, ui.general, half, H, half / 2, H / 2);
      ui.detail = detailView(ui.general, center.value_or(ui.general.target), 4.0);
    };
    resetViews(*modelFor(current));

    if (!render.empty()) {  // sin ventana
      const cv::Mat img = renderTwoViews(*modelFor(current), ui.general, ui.detail, W, H, std::numeric_limits<int>::max());
      if (!cv::imwrite(render.string(), img)) throw std::runtime_error("no se pudo escribir " + render.string());
      std::cout << "Vistas guardadas en " << render.string() << " (" << W << "x" << H << ")\n";
      return 0;
    }

    const std::string window = "Escaner 3D - visor (1/2/3 modelo, arrastrar rota, rueda zoom, clic detalle, q sale)";
    cv::namedWindow(window, cv::WINDOW_AUTOSIZE);
    cv::setMouseCallback(
        window,
        [](int event, int x, int y, int flags, void* data) {
          auto* s = static_cast<UiState*>(data);
          if (event == cv::EVENT_LBUTTONDOWN) {
            s->dragging = true, s->moved = false;
            s->last_x = s->down_x = x, s->last_y = s->down_y = y;
          } else if (event == cv::EVENT_MOUSEMOVE && s->dragging) {
            const int dx = x - s->last_x, dy = y - s->last_y;
            if (std::abs(x - s->down_x) + std::abs(y - s->down_y) > 3) s->moved = true;
            s->general.yaw_deg -= dx * 0.4;
            s->general.pitch_deg = std::clamp(s->general.pitch_deg + dy * 0.4, -85.0, 85.0);
            s->detail.yaw_deg = s->general.yaw_deg, s->detail.pitch_deg = s->general.pitch_deg;
            s->last_x = x, s->last_y = y;
            s->dirty = s->interactive = true;
          } else if (event == cv::EVENT_LBUTTONUP) {
            s->dragging = false;
            s->dirty = true;
            s->interactive = false;
            if (!s->moved) s->down_x = -x - 1;  // marca de clic (se resuelve en el bucle)
          } else if (event == cv::EVENT_MOUSEWHEEL) {
            const double f = cv::getMouseWheelDelta(flags) > 0 ? 0.85 : 1.0 / 0.85;
            if (x < 640) s->general.distance *= f; else s->detail.distance *= f;
            s->dirty = true;
          }
        },
        &ui);
    while (true) {
      if (ui.down_x < 0) {  // clic sin arrastre: elegir punto de detalle en la vista general
        const int cx = -ui.down_x - 1;
        ui.down_x = 0;
        if (cx < half)
          if (const auto p = pick(*modelFor(current), ui.general, half, H, cx, ui.last_y)) {
            ui.detail.target = *p;
            ui.dirty = true;
          }
      }
      if (ui.dirty) {
        const int max_faces = ui.interactive ? config.viewer.max_triangles_interactive : std::numeric_limits<int>::max();
        cv::imshow(window, renderTwoViews(*modelFor(current), ui.general, ui.detail, W, H, max_faces));
        ui.dirty = false;
      }
      const int key = cv::waitKey(15);
      if (key == 'q' || key == 27) break;
      if (key >= '1' && key <= '3' && modelFor(key - '0')) {
        current = key - '0';
        resetViews(*modelFor(current));
        ui.dirty = true;
      }
      if (cv::getWindowProperty(window, cv::WND_PROP_VISIBLE) < 1) break;
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
