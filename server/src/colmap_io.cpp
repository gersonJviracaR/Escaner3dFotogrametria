#include "colmap_io.h"

#include "atomic_file.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

constexpr double kPixelShift = 0.5;  // COLMAP: centro del primer píxel en (0,5, 0,5)

std::string num(double v) {
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%.17g", v);
  return buffer;
}

std::ifstream openModelFile(const fs::path& file) {
  std::ifstream in(file);
  if (!in) throw ColmapFormatError("no se pudo leer " + file.string());
  return in;
}

bool nextLine(std::istream& in, std::string& line, bool skip_empty) {
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] == '#') continue;
    if (skip_empty && line.empty()) continue;
    return true;
  }
  return false;
}

}  // namespace

ColmapModel toColmapModel(const Reconstruction& rec, const std::map<int, std::string>& names) {
  ColmapModel m;
  m.camera = rec.camera;
  std::map<int, std::size_t> slot;  // índice de foto -> posición en m.images
  for (const auto& [index, cam] : rec.cameras) {
    if (!cam.registered) continue;
    ColmapImage img;
    img.image_id = index;
    const auto it = names.find(index);
    img.name = it != names.end() ? it->second : "img_" + std::to_string(index) + ".jpg";
    img.pose = cam.pose;
    slot[index] = m.images.size();
    m.images.push_back(std::move(img));
  }
  for (std::size_t k = 0; k < rec.points.size(); ++k) {
    const Point3D& p = rec.points[k];
    ColmapPoint cp;
    cp.id = static_cast<long long>(k) + 1;
    cp.xyz = p.xyz;
    cp.rgb = p.rgb;
    double sum = 0;
    for (const Observation& o : p.observations) {
      const auto s = slot.find(o.image);
      if (s == slot.end()) continue;
      ColmapImage& img = m.images[s->second];
      cp.track.emplace_back(o.image, static_cast<int>(img.points2d.size()));
      img.points2d.push_back(o.xy);
      img.point3d_ids.push_back(cp.id);
      sum += reprojectionError(rec.camera, img.pose, p.xyz, o.xy);
    }
    if (cp.track.empty()) continue;
    cp.error = sum / static_cast<double>(cp.track.size());
    m.points.push_back(std::move(cp));
  }
  return m;
}

void writeColmapText(const fs::path& dir, const ColmapModel& m) {
  fs::create_directories(dir);
  const CameraModel& c = m.camera;
  std::string cams = "# Camera list with one line of data per camera:\n#   CAMERA_ID, MODEL, WIDTH, HEIGHT, PARAMS[]\n";
  cams += "# Exportado por Escaner 3D (Sprint 4): modelo RADIAL f, cx, cy, k1, k2\n";
  cams += "1 RADIAL " + std::to_string(c.width) + " " + std::to_string(c.height) + " " + num(c.f) + " " +
          num(c.cx + kPixelShift) + " " + num(c.cy + kPixelShift) + " " + num(c.k1) + " " + num(c.k2) + "\n";
  writeFileAtomic(dir / "cameras.txt", cams);

  std::string images = "# Image list with two lines of data per image:\n"
                       "#   IMAGE_ID, QW, QX, QY, QZ, TX, TY, TZ, CAMERA_ID, NAME\n"
                       "#   POINTS2D[] as (X, Y, POINT3D_ID)\n";
  for (const ColmapImage& img : m.images) {
    images += std::to_string(img.image_id);
    for (double v : img.pose.q) images += " " + num(v);
    for (int k = 0; k < 3; ++k) images += " " + num(img.pose.t[k]);
    images += " " + std::to_string(img.camera_id) + " " + img.name + "\n";
    std::string pts;
    for (std::size_t k = 0; k < img.points2d.size(); ++k) {
      if (k) pts += " ";
      pts += num(img.points2d[k].x + kPixelShift) + " " + num(img.points2d[k].y + kPixelShift) + " " +
             std::to_string(img.point3d_ids[k]);
    }
    images += pts + "\n";
  }
  writeFileAtomic(dir / "images.txt", images);

  std::string points = "# 3D point list with one line of data per point:\n"
                       "#   POINT3D_ID, X, Y, Z, R, G, B, ERROR, TRACK[] as (IMAGE_ID, POINT2D_IDX)\n";
  for (const ColmapPoint& p : m.points) {
    points += std::to_string(p.id) + " " + num(p.xyz[0]) + " " + num(p.xyz[1]) + " " + num(p.xyz[2]) + " " +
              std::to_string(p.rgb[0]) + " " + std::to_string(p.rgb[1]) + " " + std::to_string(p.rgb[2]) + " " +
              num(p.error);
    for (const auto& [image_id, idx] : p.track) points += " " + std::to_string(image_id) + " " + std::to_string(idx);
    points += "\n";
  }
  writeFileAtomic(dir / "points3D.txt", points);
}

ColmapModel readColmapText(const fs::path& dir) {
  ColmapModel m;
  {
    std::ifstream in = openModelFile(dir / "cameras.txt");
    std::string line;
    if (!nextLine(in, line, true)) throw ColmapFormatError("cameras.txt sin cámaras");
    std::istringstream ss(line);
    int id = 0;
    ss >> id >> m.camera_model >> m.camera.width >> m.camera.height;
    std::vector<double> p;
    double v;
    while (ss >> v) p.push_back(v);
    CameraModel& c = m.camera;
    if (m.camera_model == "SIMPLE_PINHOLE" && p.size() == 3) {
      c.f = p[0], c.cx = p[1], c.cy = p[2];
    } else if (m.camera_model == "PINHOLE" && p.size() == 4) {
      c.f = p[0], c.cx = p[2], c.cy = p[3];  // se asume fx = fy
    } else if (m.camera_model == "SIMPLE_RADIAL" && p.size() == 4) {
      c.f = p[0], c.cx = p[1], c.cy = p[2], c.k1 = p[3];
    } else if (m.camera_model == "RADIAL" && p.size() == 5) {
      c.f = p[0], c.cx = p[1], c.cy = p[2], c.k1 = p[3], c.k2 = p[4];
    } else {
      throw ColmapFormatError("modelo de cámara no soportado: " + m.camera_model);
    }
    c.cx -= kPixelShift;
    c.cy -= kPixelShift;
  }
  {
    std::ifstream in = openModelFile(dir / "images.txt");
    std::string header, points;
    while (nextLine(in, header, true)) {
      ColmapImage img;
      std::istringstream ss(header);
      if (!(ss >> img.image_id >> img.pose.q[0] >> img.pose.q[1] >> img.pose.q[2] >> img.pose.q[3] >> img.pose.t[0] >>
            img.pose.t[1] >> img.pose.t[2] >> img.camera_id >> img.name))
        throw ColmapFormatError("línea de imagen inválida en images.txt: " + header);
      if (!std::getline(in, points)) points.clear();
      if (!points.empty() && points.back() == '\r') points.pop_back();
      std::istringstream ps(points);
      double x, y;
      long long pid;
      while (ps >> x >> y >> pid) {
        img.points2d.emplace_back(x - kPixelShift, y - kPixelShift);
        img.point3d_ids.push_back(pid);
      }
      m.images.push_back(std::move(img));
    }
    std::sort(m.images.begin(), m.images.end(),
              [](const ColmapImage& a, const ColmapImage& b) { return a.image_id < b.image_id; });
  }
  {
    std::ifstream in = openModelFile(dir / "points3D.txt");
    std::string line;
    while (nextLine(in, line, true)) {
      std::istringstream ss(line);
      ColmapPoint p;
      int r, g, b;
      if (!(ss >> p.id >> p.xyz[0] >> p.xyz[1] >> p.xyz[2] >> r >> g >> b >> p.error))
        throw ColmapFormatError("línea de punto inválida en points3D.txt: " + line);
      p.rgb = cv::Vec3b(static_cast<uchar>(r), static_cast<uchar>(g), static_cast<uchar>(b));
      int image_id, idx;
      while (ss >> image_id >> idx) p.track.emplace_back(image_id, idx);
      m.points.push_back(std::move(p));
    }
  }
  return m;
}

Reconstruction toReconstruction(const ColmapModel& m, const std::map<std::string, int>& index_of) {
  Reconstruction rec;
  rec.camera = m.camera;
  std::map<int, const ColmapImage*> by_id;
  std::map<int, int> index_by_id;
  for (const ColmapImage& img : m.images) {
    const auto it = index_of.find(img.name);
    if (it == index_of.end()) continue;
    Camera& cam = rec.cameras[it->second];
    cam.index = it->second;
    cam.registered = true;
    cam.pose = img.pose;
    rec.registration_order.push_back(it->second);
    by_id[img.image_id] = &img;
    index_by_id[img.image_id] = it->second;
  }
  for (const ColmapPoint& cp : m.points) {
    Point3D p;
    p.xyz = cp.xyz;
    p.rgb = cp.rgb;
    for (const auto& [image_id, idx] : cp.track) {
      const auto it = by_id.find(image_id);
      if (it == by_id.end() || idx < 0 || idx >= static_cast<int>(it->second->points2d.size())) continue;
      p.observations.push_back({index_by_id[image_id], idx, it->second->points2d[static_cast<std::size_t>(idx)]});
    }
    if (!p.observations.empty()) rec.points.push_back(std::move(p));
  }
  return rec;
}
