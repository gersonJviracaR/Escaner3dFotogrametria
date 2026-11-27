#include "obj_export.h"

#include "atomic_file.h"

#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
  const std::size_t a = s.find_first_not_of(" \t");
  const std::size_t b = s.find_last_not_of(" \t");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

double parseNumber(const std::string& s) {
  std::size_t used = 0;
  double v = 0;
  try {
    v = std::stod(trim(s), &used);
  } catch (const std::exception&) {
    throw std::invalid_argument("número inválido en --scale-ref: " + s);
  }
  if (used != trim(s).size()) throw std::invalid_argument("número inválido en --scale-ref: " + s);
  return v;
}

cv::Vec3d parsePoint(const std::string& s) {
  std::vector<double> c;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, ',')) c.push_back(parseNumber(part));
  if (c.size() != 3) throw std::invalid_argument("un punto de --scale-ref debe tener 3 coordenadas: " + s);
  return {c[0], c[1], c[2]};
}

}  // namespace

ScaleSpec parseScaleRef(const std::string& text) {
  std::vector<std::string> parts;
  std::stringstream ss(text);
  std::string part;
  while (std::getline(ss, part, ';')) parts.push_back(part);
  if (parts.size() != 3) throw std::invalid_argument("--scale-ref debe ser \"x1,y1,z1;x2,y2,z2;D_mm\"");
  ScaleSpec s;
  s.kind = ScaleSpec::Kind::Reference;
  s.p1 = parsePoint(parts[0]);
  s.p2 = parsePoint(parts[1]);
  s.distance_mm = parseNumber(parts[2]);
  if (!(s.distance_mm > 0)) throw std::invalid_argument("la distancia real de --scale-ref debe ser positiva");
  if (!(cv::norm(s.p1 - s.p2) > 1e-12)) throw std::invalid_argument("los dos puntos de --scale-ref coinciden (distancia 0)");
  return s;
}

ScaleSpec scaleByFactor(double factor) {
  if (!(factor > 0)) throw std::invalid_argument("--scale debe ser positivo");
  ScaleSpec s;
  s.kind = ScaleSpec::Kind::Factor;
  s.factor = factor;
  return s;
}

double scaleFactor(const ScaleSpec& spec) {
  switch (spec.kind) {
    case ScaleSpec::Kind::Reference:
      return spec.distance_mm / cv::norm(spec.p1 - spec.p2);
    case ScaleSpec::Kind::Factor:
      return spec.factor;
    default:
      return 1.0;
  }
}

std::string objText(const Mesh& mesh, const TextureResult& texture, double scale, const std::string& comment) {
  const bool normals = mesh.normals.size() == mesh.vertices.size() && !mesh.normals.empty();
  std::string out = "# Escaner 3D por Fotogrametria (Sprint 4)\n";
  if (!comment.empty()) out += "# " + comment + "\n";
  out += "mtllib model.mtl\n";
  char line[160];
  for (const cv::Vec3f& v : mesh.vertices) {
    std::snprintf(line, sizeof(line), "v %.7g %.7g %.7g\n", v[0] * scale, v[1] * scale, v[2] * scale);
    out += line;
  }
  for (std::size_t k = 0; k < mesh.faces.size(); ++k)
    for (int q = 0; q < 3; ++q) {
      const cv::Vec2f t = k < texture.face_uv.size() ? texture.face_uv[k][static_cast<std::size_t>(q)] : cv::Vec2f(0, 0);
      std::snprintf(line, sizeof(line), "vt %.7g %.7g\n", t[0], t[1]);
      out += line;
    }
  if (normals)
    for (const cv::Vec3f& n : mesh.normals) {
      std::snprintf(line, sizeof(line), "vn %.6g %.6g %.6g\n", n[0], n[1], n[2]);
      out += line;
    }
  out += "usemtl material0\n";
  for (std::size_t k = 0; k < mesh.faces.size(); ++k) {
    out += "f";
    for (int q = 0; q < 3; ++q) {
      const int v = mesh.faces[k][q] + 1;
      const long long vt = static_cast<long long>(3 * k) + q + 1;
      if (normals)
        std::snprintf(line, sizeof(line), " %d/%lld/%d", v, vt, v);
      else
        std::snprintf(line, sizeof(line), " %d/%lld", v, vt);
      out += line;
    }
    out += "\n";
  }
  return out;
}

std::string mtlText(const std::string& texture_file) {
  return "# Escaner 3D por Fotogrametria (Sprint 4)\nnewmtl material0\nKa 1 1 1\nKd 1 1 1\nKs 0 0 0\nd 1\nillum 1\n"
         "map_Kd " +
         texture_file + "\n";
}

ObjExport exportObj(const fs::path& dir, const Mesh& mesh, const TextureResult& texture, const ScaleSpec& scale) {
  fs::create_directories(dir);
  ObjExport r;
  r.scale = scaleFactor(scale);
  r.unit = scale.kind == ScaleSpec::Kind::Reference ? "mm" : "arbitraria";
  if (scale.kind == ScaleSpec::Kind::Factor) r.unit = "arbitraria x factor";
  char comment[200];
  if (scale.kind == ScaleSpec::Kind::None)
    std::snprintf(comment, sizeof(comment), "escala arbitraria (monocular); factor 1");
  else
    std::snprintf(comment, sizeof(comment), "unidad: %s; factor de escala aplicado: %.9g", r.unit.c_str(), r.scale);
  writeFileAtomic(dir / "model.obj", objText(mesh, texture, r.scale, comment));
  writeFileAtomic(dir / "model.mtl", mtlText("texture.png"));
  std::vector<uchar> png;
  cv::imencode(".png", texture.atlas.empty() ? cv::Mat(4, 4, CV_8UC3, cv::Scalar(128, 128, 128)) : texture.atlas, png);
  writeFileAtomic(dir / "texture.png", std::string(png.begin(), png.end()));
  const MeshMetrics mm = meshMetrics(mesh);
  r.vertices = mm.vertices;
  r.faces = mm.faces;
  r.closed = mm.closed;
  r.open_edges = mm.open_edges;
  return r;
}
