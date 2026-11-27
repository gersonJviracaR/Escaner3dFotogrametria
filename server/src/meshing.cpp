#include "meshing.h"

#include "atomic_file.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

int typeSize(const std::string& t) {
  if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
  if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
  if (t == "int" || t == "uint" || t == "int32" || t == "uint32" || t == "float" || t == "float32") return 4;
  if (t == "double" || t == "float64") return 8;
  throw MeshFormatError("tipo de propiedad PLY desconocido: " + t);
}

double readBinary(const char* p, const std::string& t) {
  if (t == "char" || t == "int8") return static_cast<double>(*reinterpret_cast<const std::int8_t*>(p));
  if (t == "uchar" || t == "uint8") return static_cast<double>(*reinterpret_cast<const std::uint8_t*>(p));
  std::int16_t s16;
  std::uint16_t u16;
  std::int32_t s32;
  std::uint32_t u32;
  float f;
  double d;
  if (t == "short" || t == "int16") return std::memcpy(&s16, p, 2), s16;
  if (t == "ushort" || t == "uint16") return std::memcpy(&u16, p, 2), u16;
  if (t == "int" || t == "int32") return std::memcpy(&s32, p, 4), s32;
  if (t == "uint" || t == "uint32") return std::memcpy(&u32, p, 4), u32;
  if (t == "float" || t == "float32") return std::memcpy(&f, p, 4), f;
  return std::memcpy(&d, p, 8), d;
}

struct Property {
  std::string name, type;
  bool is_list = false;
  std::string count_type, index_type;
};

struct Element {
  std::string name;
  long long count = 0;
  std::vector<Property> props;
};

template <class T>
void put(std::string& out, T v) {
  out.append(reinterpret_cast<const char*>(&v), sizeof(T));
}

}  // namespace

Mesh readPlyMesh(const fs::path& file) {
  std::string bytes;
  try {
    bytes = readFileBytes(file);
  } catch (const std::exception& e) {
    throw MeshFormatError(e.what());
  }
  std::size_t pos = 0;
  auto headerLine = [&]() {
    const std::size_t end = bytes.find('\n', pos);
    if (end == std::string::npos) throw MeshFormatError("cabecera PLY incompleta en " + file.string());
    std::string l = bytes.substr(pos, end - pos);
    pos = end + 1;
    if (!l.empty() && l.back() == '\r') l.pop_back();
    return l;
  };
  if (headerLine() != "ply") throw MeshFormatError("no es un archivo PLY: " + file.string());
  std::string format;
  std::vector<Element> elements;
  for (std::string l = headerLine(); l != "end_header"; l = headerLine()) {
    std::istringstream ss(l);
    std::string word;
    ss >> word;
    if (word == "format") {
      ss >> format;
    } else if (word == "element") {
      Element e;
      ss >> e.name >> e.count;
      elements.push_back(e);
    } else if (word == "property" && !elements.empty()) {
      Property p;
      ss >> p.type;
      if (p.type == "list") {
        p.is_list = true;
        ss >> p.count_type >> p.index_type >> p.name;
      } else {
        ss >> p.name;
      }
      elements.back().props.push_back(p);
    }
  }
  const bool ascii = format == "ascii";
  if (!ascii && format != "binary_little_endian") throw MeshFormatError("formato PLY no soportado: " + format);

  Mesh m;
  std::istringstream text(ascii ? bytes.substr(pos) : std::string());
  auto readValue = [&](const std::string& type) -> double {
    if (ascii) {
      double v;
      if (!(text >> v)) throw MeshFormatError("datos PLY truncados en " + file.string());
      return v;
    }
    const int n = typeSize(type);
    if (pos + static_cast<std::size_t>(n) > bytes.size()) throw MeshFormatError("datos PLY truncados en " + file.string());
    const double v = readBinary(bytes.data() + pos, type);
    pos += static_cast<std::size_t>(n);
    return v;
  };
  for (const Element& e : elements) {
    const bool is_vertex = e.name == "vertex";
    const bool is_face = e.name == "face";
    std::map<std::string, std::size_t> idx;
    for (std::size_t k = 0; k < e.props.size(); ++k) idx[e.props[k].name] = k;
    const bool has_normals = idx.count("nx") && idx.count("ny") && idx.count("nz");
    const bool has_colors = idx.count("red") && idx.count("green") && idx.count("blue");
    std::vector<double> values(e.props.size());
    for (long long r = 0; r < e.count; ++r) {
      std::vector<int> list;
      for (std::size_t k = 0; k < e.props.size(); ++k) {
        const Property& p = e.props[k];
        if (p.is_list) {
          const int n = static_cast<int>(readValue(p.count_type));
          list.clear();
          for (int q = 0; q < n; ++q) list.push_back(static_cast<int>(readValue(p.index_type)));
        } else {
          values[k] = readValue(p.type);
        }
      }
      if (is_vertex) {
        m.vertices.emplace_back(static_cast<float>(values[idx.at("x")]), static_cast<float>(values[idx.at("y")]),
                                static_cast<float>(values[idx.at("z")]));
        if (has_normals)
          m.normals.emplace_back(static_cast<float>(values[idx["nx"]]), static_cast<float>(values[idx["ny"]]),
                                 static_cast<float>(values[idx["nz"]]));
        if (has_colors)
          m.colors.emplace_back(static_cast<uchar>(values[idx["red"]]), static_cast<uchar>(values[idx["green"]]),
                                static_cast<uchar>(values[idx["blue"]]));
      } else if (is_face) {
        for (std::size_t q = 2; q < list.size(); ++q) m.faces.emplace_back(list[0], list[q - 1], list[q]);
      }
    }
  }
  for (const cv::Vec3i& f : m.faces)
    for (int k = 0; k < 3; ++k)
      if (f[k] < 0 || f[k] >= static_cast<int>(m.vertices.size()))
        throw MeshFormatError("índice de cara fuera de rango en " + file.string());
  return m;
}

void writePlyMesh(const fs::path& file, const Mesh& m) {
  const bool normals = m.normals.size() == m.vertices.size() && !m.normals.empty();
  const bool colors = m.colors.size() == m.vertices.size() && !m.colors.empty();
  std::string out = "ply\nformat binary_little_endian 1.0\ncomment Escaner 3D (Sprint 4)\n";
  out += "element vertex " + std::to_string(m.vertices.size()) + "\nproperty float x\nproperty float y\nproperty float z\n";
  if (normals) out += "property float nx\nproperty float ny\nproperty float nz\n";
  if (colors) out += "property uchar red\nproperty uchar green\nproperty uchar blue\n";
  out += "element face " + std::to_string(m.faces.size()) + "\nproperty list uchar int vertex_indices\nend_header\n";
  for (std::size_t k = 0; k < m.vertices.size(); ++k) {
    for (int c = 0; c < 3; ++c) put(out, m.vertices[k][c]);
    if (normals)
      for (int c = 0; c < 3; ++c) put(out, m.normals[k][c]);
    if (colors)
      for (int c = 0; c < 3; ++c) put(out, m.colors[k][c]);
  }
  for (const cv::Vec3i& f : m.faces) {
    put(out, static_cast<std::uint8_t>(3));
    for (int c = 0; c < 3; ++c) put(out, static_cast<std::int32_t>(f[c]));
  }
  fs::create_directories(file.parent_path());
  writeFileAtomic(file, out);
}

namespace {

double faceArea(const Mesh& m, const cv::Vec3i& f) {
  const cv::Vec3d a(m.vertices[f[0]]), b(m.vertices[f[1]]), c(m.vertices[f[2]]);
  return 0.5 * cv::norm((b - a).cross(c - a));
}

std::vector<int> faceComponents(const Mesh& m, int& n_components) {
  std::vector<int> parent(m.vertices.size());
  std::iota(parent.begin(), parent.end(), 0);
  auto find = [&](int x) {
    while (parent[x] != x) x = parent[x] = parent[parent[x]];
    return x;
  };
  for (const cv::Vec3i& f : m.faces) {
    parent[find(f[1])] = find(f[0]);
    parent[find(f[2])] = find(f[0]);
  }
  std::map<int, int> label;
  std::vector<int> comp(m.faces.size());
  for (std::size_t k = 0; k < m.faces.size(); ++k) {
    const int root = find(m.faces[k][0]);
    const auto it = label.emplace(root, static_cast<int>(label.size())).first;
    comp[k] = it->second;
  }
  n_components = static_cast<int>(label.size());
  return comp;
}

}  // namespace

CleanStats cleanMesh(Mesh& m, int min_component_faces) {
  CleanStats s;
  // 1. Caras degeneradas.
  double mean_area = 0;
  for (const cv::Vec3i& f : m.faces) mean_area += faceArea(m, f);
  mean_area /= std::max<std::size_t>(1, m.faces.size());
  const double eps = 1e-12 * std::max(1.0, mean_area);
  std::vector<cv::Vec3i> kept;
  for (const cv::Vec3i& f : m.faces) {
    if (f[0] == f[1] || f[1] == f[2] || f[0] == f[2] || faceArea(m, f) <= eps) {
      ++s.degenerate_faces;
      continue;
    }
    kept.push_back(f);
  }
  m.faces = std::move(kept);

  // 2. Componentes pequeñas.
  int n = 0;
  const std::vector<int> comp = faceComponents(m, n);
  std::vector<int> size(static_cast<std::size_t>(n), 0);
  for (int c : comp) ++size[static_cast<std::size_t>(c)];
  for (int c = 0; c < n; ++c)
    if (size[static_cast<std::size_t>(c)] < min_component_faces) ++s.small_components;
  kept.clear();
  for (std::size_t k = 0; k < m.faces.size(); ++k) {
    if (size[static_cast<std::size_t>(comp[k])] < min_component_faces) {
      ++s.small_component_faces;
      continue;
    }
    kept.push_back(m.faces[k]);
  }
  m.faces = std::move(kept);

  // 3. Vértices sin caras (se reindexa).
  std::vector<int> remap(m.vertices.size(), -1);
  for (const cv::Vec3i& f : m.faces)
    for (int k = 0; k < 3; ++k) remap[static_cast<std::size_t>(f[k])] = 0;
  Mesh out;
  for (std::size_t v = 0; v < m.vertices.size(); ++v) {
    if (remap[v] < 0) {
      ++s.unreferenced_vertices;
      continue;
    }
    remap[v] = static_cast<int>(out.vertices.size());
    out.vertices.push_back(m.vertices[v]);
    if (m.normals.size() == m.vertices.size()) out.normals.push_back(m.normals[v]);
    if (m.colors.size() == m.vertices.size()) out.colors.push_back(m.colors[v]);
  }
  for (const cv::Vec3i& f : m.faces)
    out.faces.emplace_back(remap[static_cast<std::size_t>(f[0])], remap[static_cast<std::size_t>(f[1])],
                           remap[static_cast<std::size_t>(f[2])]);
  m = std::move(out);
  return s;
}

MeshMetrics meshMetrics(const Mesh& m) {
  MeshMetrics r;
  r.vertices = static_cast<int>(m.vertices.size());
  r.faces = static_cast<int>(m.faces.size());
  std::map<std::pair<int, int>, int> edges;
  for (const cv::Vec3i& f : m.faces) {
    r.area += faceArea(m, f);
    for (int k = 0; k < 3; ++k) {
      const int a = f[k], b = f[(k + 1) % 3];
      ++edges[{std::min(a, b), std::max(a, b)}];
    }
  }
  for (const auto& [e, count] : edges) r.open_edges += count == 1 ? 1 : 0;
  faceComponents(m, r.components);
  r.closed = r.faces > 0 && r.open_edges == 0;
  return r;
}

json toJson(const MeshSummary& s) {
  return json{{"raw_vertices", s.raw_vertices},
              {"raw_faces", s.raw_faces},
              {"cleaning",
               {{"degenerate_faces", s.cleaning.degenerate_faces},
                {"small_components", s.cleaning.small_components},
                {"small_component_faces", s.cleaning.small_component_faces},
                {"unreferenced_vertices", s.cleaning.unreferenced_vertices}}},
              {"vertices", s.vertices},
              {"faces", s.faces},
              {"components", s.components},
              {"open_edges", s.open_edges},
              {"closed", s.closed},
              {"area", s.area},
              {"poisson_ms", s.poisson_ms},
              {"clean_ms", s.clean_ms}};
}

MeshSummary runMeshing(ColmapRunner& colmap, const fs::path& fused_ply, const fs::path& model_dir,
                       const MeshParams& params) {
  fs::create_directories(model_dir);
  const fs::path raw = model_dir / "mesh_raw.ply";
  MeshSummary s;
  char trim[32];
  std::snprintf(trim, sizeof(trim), "%g", params.trim_density);
  s.poisson_ms = colmap
                     .run("poisson_mesher", {{"--input_path", fused_ply.string()},
                                             {"--output_path", raw.string()},
                                             {"--PoissonMeshing.depth", std::to_string(params.poisson_depth)},
                                             {"--PoissonMeshing.trim", trim}})
                     .ms;
  const auto t0 = std::chrono::steady_clock::now();
  Mesh mesh = readPlyMesh(raw);
  s.raw_vertices = static_cast<int>(mesh.vertices.size());
  s.raw_faces = static_cast<int>(mesh.faces.size());
  s.cleaning = cleanMesh(mesh, params.min_component_faces);
  writePlyMesh(model_dir / "mesh.ply", mesh);
  const MeshMetrics mm = meshMetrics(mesh);
  s.vertices = mm.vertices;
  s.faces = mm.faces;
  s.components = mm.components;
  s.open_edges = mm.open_edges;
  s.closed = mm.closed;
  s.area = mm.area;
  s.clean_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return s;
}
