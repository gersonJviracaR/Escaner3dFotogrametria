// Pruebas de HU-20: lectura/escritura de mallas PLY, limpieza y métricas.
#include "meshing.h"
#include "test_meshes.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

fs::path tempDir(const std::string& name) {
  const fs::path d = fs::temp_directory_path() /
                     (name + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(d);
  return d;
}

}  // namespace

TEST(Meshing, PlyRoundTripBinaryWithNormalsAndColors) {
  Mesh m = testmesh::cube();
  for (std::size_t k = 0; k < m.vertices.size(); ++k) {
    m.normals.push_back(cv::normalize(m.vertices[k] - cv::Vec3f(0.5f, 0.5f, 0.5f)));
    m.colors.emplace_back(static_cast<uchar>(10 * k), 100, 200);
  }
  const fs::path dir = tempDir("escaner3d_mesh_");
  writePlyMesh(dir / "m.ply", m);
  const Mesh back = readPlyMesh(dir / "m.ply");
  fs::remove_all(dir);
  ASSERT_EQ(back.vertices.size(), m.vertices.size());
  ASSERT_EQ(back.faces.size(), m.faces.size());
  ASSERT_EQ(back.normals.size(), m.normals.size());
  ASSERT_EQ(back.colors.size(), m.colors.size());
  for (std::size_t k = 0; k < m.vertices.size(); ++k) {
    EXPECT_EQ(back.vertices[k], m.vertices[k]);
    EXPECT_EQ(back.colors[k], m.colors[k]);
  }
  for (std::size_t k = 0; k < m.faces.size(); ++k) EXPECT_EQ(back.faces[k], m.faces[k]);
}

TEST(Meshing, ReadsAsciiPlyWithExtraPropertiesAndQuads) {
  const fs::path dir = tempDir("escaner3d_mesh_");
  std::ofstream(dir / "a.ply") << "ply\nformat ascii 1.0\ncomment de prueba\nelement vertex 4\n"
                                  "property double x\nproperty double y\nproperty double z\nproperty float value\n"
                                  "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
                                  "0 0 0 1\n1 0 0 1\n1 1 0 1\n0 1 0 1\n4 0 1 2 3\n";
  const Mesh m = readPlyMesh(dir / "a.ply");
  EXPECT_EQ(m.vertices.size(), 4u);
  ASSERT_EQ(m.faces.size(), 2u);  // cuadrilátero en abanico
  EXPECT_EQ(m.faces[1], cv::Vec3i(0, 2, 3));
  std::ofstream(dir / "b.ply") << "no es un ply\n";
  EXPECT_THROW(readPlyMesh(dir / "b.ply"), MeshFormatError);
  EXPECT_THROW(readPlyMesh(dir / "no_existe.ply"), MeshFormatError);
  fs::remove_all(dir);
}

TEST(Meshing, CleanRemovesDegenerateAndSmallComponentsKeepsMainMesh) {
  Mesh m = testmesh::cube();  // 12 caras
  const int n = static_cast<int>(m.vertices.size());
  m.faces.emplace_back(0, 0, 1);  // índice repetido
  m.vertices.push_back(m.vertices[1]);  // vértice repetido en la misma posición
  m.faces.emplace_back(0, 1, n);        // área 0
  // Componente suelta de 1 triángulo, lejos.
  m.vertices.push_back({10, 10, 10});
  m.vertices.push_back({11, 10, 10});
  m.vertices.push_back({10, 11, 10});
  m.faces.emplace_back(n + 1, n + 2, n + 3);
  const CleanStats s = cleanMesh(m, 5);
  EXPECT_EQ(s.degenerate_faces, 2);
  EXPECT_EQ(s.small_components, 1);
  EXPECT_EQ(s.small_component_faces, 1);
  EXPECT_EQ(s.unreferenced_vertices, 4);
  EXPECT_EQ(m.faces.size(), 12u);
  EXPECT_EQ(m.vertices.size(), 8u);
  for (const cv::Vec3i& f : m.faces)
    for (int k = 0; k < 3; ++k) EXPECT_LT(f[k], 8);
  const MeshMetrics mm = meshMetrics(m);
  EXPECT_TRUE(mm.closed);
}

TEST(Meshing, MetricsOfClosedCubeAndOpenPlane) {
  const MeshMetrics cube = meshMetrics(testmesh::cube());
  EXPECT_EQ(cube.vertices, 8);
  EXPECT_EQ(cube.faces, 12);
  EXPECT_EQ(cube.components, 1);
  EXPECT_EQ(cube.open_edges, 0);
  EXPECT_TRUE(cube.closed);
  EXPECT_NEAR(cube.area, 6.0, 1e-6);
  const MeshMetrics plane = meshMetrics(testmesh::plane());
  EXPECT_EQ(plane.open_edges, 4);
  EXPECT_FALSE(plane.closed);
  EXPECT_NEAR(plane.area, 1.0, 1e-6);
}

TEST(Meshing, RunMeshingCallsPoissonAndWritesCleanMesh) {
  const fs::path dir = tempDir("escaner3d_meshing_");
  _putenv_s("COLMAP_STUB_LOG", (dir / "args.txt").string().c_str());
  ColmapRunner colmap(COLMAP_STUB_PATH, dir / "colmap.log");
  MeshParams p;
  p.min_component_faces = 0;
  const MeshSummary s = runMeshing(colmap, dir / "fused.ply", dir / "model", p);
  std::string args;
  {
    std::ifstream in(dir / "args.txt");  // se cierra antes de borrar la carpeta (Windows)
    std::stringstream ss;
    ss << in.rdbuf();
    args = ss.str();
  }
  EXPECT_NE(args.find("poisson_mesher\n--input_path\n" + (dir / "fused.ply").string()), std::string::npos);
  EXPECT_NE(args.find("--output_path\n" + (dir / "model" / "mesh_raw.ply").string()), std::string::npos);
  EXPECT_NE(args.find("--PoissonMeshing.depth\n9"), std::string::npos);
  EXPECT_NE(args.find("--PoissonMeshing.trim\n7"), std::string::npos);
  EXPECT_TRUE(fs::exists(dir / "model" / "mesh.ply"));
  EXPECT_EQ(s.raw_faces, 1);  // el PLY del stub
  EXPECT_EQ(s.faces, 1);
  EXPECT_EQ(toJson(s).at("faces"), 1);
  fs::remove_all(dir);
}
