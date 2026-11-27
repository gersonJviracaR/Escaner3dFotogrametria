// Malla triangulada desde la nube densa (HU-20): poisson_mesher de COLMAP y limpieza propia.
#pragma once

#include "colmap_runner.h"
#include "pipeline_params.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <filesystem>
#include <stdexcept>
#include <vector>

class MeshFormatError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct Mesh {
  std::vector<cv::Vec3f> vertices;
  std::vector<cv::Vec3f> normals;  // vacío o uno por vértice
  std::vector<cv::Vec3b> colors;   // vacío o uno por vértice (R, G, B)
  std::vector<cv::Vec3i> faces;    // índices base 0
};

// PLY ascii o binary_little_endian; propiedades de vértice x, y, z (float o double),
// nx, ny, nz, red, green, blue (las demás se ignoran); caras como lista de índices.
// Los polígonos de más de 3 vértices se triangulan en abanico.
Mesh readPlyMesh(const std::filesystem::path& file);
// PLY binary_little_endian con normales y colores si los hay (escritura segura).
void writePlyMesh(const std::filesystem::path& file, const Mesh& mesh);

struct CleanStats {
  int degenerate_faces = 0;        // índices repetidos o área ~0
  int small_component_faces = 0;   // caras de componentes con menos de min_component_faces
  int small_components = 0;
  int unreferenced_vertices = 0;
};

// Quita caras degeneradas, componentes (conexas por vértices) con menos de
// min_component_faces caras y vértices sin caras.
CleanStats cleanMesh(Mesh& mesh, int min_component_faces);

struct MeshMetrics {
  int vertices = 0;
  int faces = 0;
  int components = 0;
  int open_edges = 0;  // aristas usadas por una sola cara
  double area = 0;
  bool closed = false; // sin aristas abiertas
};
MeshMetrics meshMetrics(const Mesh& mesh);

struct MeshSummary {
  int raw_vertices = 0;
  int raw_faces = 0;
  CleanStats cleaning;
  int vertices = 0;
  int faces = 0;
  int components = 0;
  int open_edges = 0;
  double area = 0;  // en las unidades de la reconstrucción (escala arbitraria)
  bool closed = false;
  double poisson_ms = 0;
  double clean_ms = 0;
};
nlohmann::json toJson(const MeshSummary& s);

// poisson_mesher (PoissonMeshing.depth y .trim) -> model_dir/mesh_raw.ply; limpieza ->
// model_dir/mesh.ply.
MeshSummary runMeshing(ColmapRunner& colmap, const std::filesystem::path& fused_ply,
                       const std::filesystem::path& model_dir, const MeshParams& params);
