// Pruebas de HU-19: exportación a formato COLMAP, ejecución de COLMAP (con un ejecutable
// falso) y orquestación de la densificación.
#include "colmap_io.h"
#include "colmap_runner.h"
#include "dense.h"
#include "test_scene.h"

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

std::string readAll(const fs::path& f) {
  std::ifstream in(f);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<std::string> nonCommentLines(const fs::path& f) {
  std::vector<std::string> lines;
  std::ifstream in(f);
  std::string l;
  while (std::getline(in, l))
    if (!l.empty() && l[0] != '#') lines.push_back(l);
  return lines;
}

// Reconstrucción con dos cámaras registradas, una sin registrar y dos puntos.
Reconstruction sample() {
  Reconstruction rec;
  rec.camera = scene::camera(3079.25);
  rec.camera.cx = 2000;
  rec.camera.cy = 1500;
  rec.camera.k1 = 0.105;
  rec.camera.k2 = -0.28;
  rec.camera.width = 4000;
  rec.camera.height = 3000;
  rec.cameras[1] = {1, true, "", Pose{}};
  rec.cameras[2] = {2, true, "", scene::lookAt({1, -5, 0.3})};
  rec.cameras[5] = {5, false, "componente_desconectado", Pose{}};
  Point3D a;
  a.xyz = {0.1, 0.2, 4.0};
  a.rgb = {10, 20, 30};
  a.observations = {{1, 11, {100.0, 200.0}}, {2, 21, {110.0, 205.0}}};
  Point3D b;
  b.xyz = {-0.3, 0.1, 5.0};
  b.rgb = {200, 100, 50};
  b.observations = {{1, 12, {300.25, 400.5}}, {2, 22, {290.0, 410.0}}};
  rec.points = {a, b};
  return rec;
}

const std::map<int, std::string> kNames = {{1, "img_001.jpg"}, {2, "img_002.jpg"}, {5, "img_005.jpg"}};

void setEnv(const char* name, const std::string& value) { _putenv_s(name, value.c_str()); }

}  // namespace

// ---------- Exportación y lectura del modelo de texto ----------

TEST(ColmapIo, CamerasTxtUsesRadialWithShiftedPrincipalPoint) {
  const fs::path dir = tempDir("escaner3d_colmap_");
  writeColmapText(dir, toColmapModel(sample(), kNames));
  const std::vector<std::string> cams = nonCommentLines(dir / "cameras.txt");
  ASSERT_EQ(cams.size(), 1u);
  std::istringstream ss(cams[0]);
  int id, w, h;
  std::string model;
  double f, cx, cy, k1, k2;
  ss >> id >> model >> w >> h >> f >> cx >> cy >> k1 >> k2;
  EXPECT_EQ(model, "RADIAL");
  EXPECT_EQ(w, 4000);
  EXPECT_EQ(h, 3000);
  EXPECT_DOUBLE_EQ(f, 3079.25);
  EXPECT_DOUBLE_EQ(cx, 2000.5);  // centro del primer píxel en (0,5, 0,5) en COLMAP
  EXPECT_DOUBLE_EQ(cy, 1500.5);
  EXPECT_DOUBLE_EQ(k1, 0.105);
  EXPECT_DOUBLE_EQ(k2, -0.28);
  fs::remove_all(dir);
}

TEST(ColmapIo, ImagesTxtHasPoseUnchangedAndOnlyRegisteredCameras) {
  const Reconstruction rec = sample();
  const fs::path dir = tempDir("escaner3d_colmap_");
  writeColmapText(dir, toColmapModel(rec, kNames));
  const std::vector<std::string> lines = nonCommentLines(dir / "images.txt");
  ASSERT_EQ(lines.size(), 4u);  // 2 imágenes x (pose + puntos 2D)
  std::istringstream ss(lines[2]);
  int id, cam;
  double qw, qx, qy, qz, tx, ty, tz;
  std::string name;
  ss >> id >> qw >> qx >> qy >> qz >> tx >> ty >> tz >> cam >> name;
  const Pose& p = rec.cameras.at(2).pose;
  EXPECT_EQ(id, 2);
  EXPECT_EQ(name, "img_002.jpg");
  EXPECT_DOUBLE_EQ(qw, p.q[0]);
  EXPECT_DOUBLE_EQ(qx, p.q[1]);
  EXPECT_DOUBLE_EQ(qy, p.q[2]);
  EXPECT_DOUBLE_EQ(qz, p.q[3]);
  EXPECT_DOUBLE_EQ(tx, p.t[0]);
  EXPECT_DOUBLE_EQ(tz, p.t[2]);
  EXPECT_EQ(readAll(dir / "images.txt").find("img_005.jpg"), std::string::npos);
  // Puntos 2D: "X Y POINT3D_ID" con +0,5.
  std::istringstream pts(lines[1]);
  double x, y;
  long long pid;
  pts >> x >> y >> pid;
  EXPECT_DOUBLE_EQ(x, 100.5);
  EXPECT_DOUBLE_EQ(y, 200.5);
  EXPECT_GE(pid, 1);
  fs::remove_all(dir);
}

TEST(ColmapIo, Points3DTracksAreCoherentAndRoundTrip) {
  const Reconstruction rec = sample();
  const fs::path dir = tempDir("escaner3d_colmap_");
  const ColmapModel model = toColmapModel(rec, kNames);
  writeColmapText(dir, model);
  const ColmapModel back = readColmapText(dir);
  fs::remove_all(dir);

  EXPECT_EQ(back.camera_model, "RADIAL");
  EXPECT_DOUBLE_EQ(back.camera.f, rec.camera.f);
  EXPECT_DOUBLE_EQ(back.camera.cx, 2000.0);  // de vuelta a la convención propia
  EXPECT_DOUBLE_EQ(back.camera.k2, rec.camera.k2);
  ASSERT_EQ(back.images.size(), 2u);
  ASSERT_EQ(back.points.size(), 2u);
  for (const ColmapPoint& p : back.points) {
    ASSERT_EQ(p.track.size(), 2u);
    for (const auto& [image_id, idx] : p.track) {
      const ColmapImage& img = back.images[static_cast<std::size_t>(image_id - 1)];
      ASSERT_EQ(img.image_id, image_id);
      EXPECT_EQ(img.point3d_ids[static_cast<std::size_t>(idx)], p.id);  // la pista apunta a su punto 2D
    }
  }
  EXPECT_EQ(back.points[1].rgb, cv::Vec3b(200, 100, 50));
  EXPECT_NEAR(back.images[0].points2d[1].x, 300.25, 1e-9);
  for (int k = 0; k < 4; ++k) EXPECT_DOUBLE_EQ(back.images[1].pose.q[k], rec.cameras.at(2).pose.q[k]);
}

TEST(ColmapIo, ReadsSimpleRadialAndConvertsToReconstruction) {
  const fs::path dir = tempDir("escaner3d_colmap_");
  std::ofstream(dir / "cameras.txt") << "# c\n1 SIMPLE_RADIAL 4000 3000 3093.2 2000.5 1500.5 -0.002\n";
  std::ofstream(dir / "images.txt")
      << "# i\n7 1 0 0 0 0.5 0 0 1 img_009.jpg\n10.5 20.5 -1\n3 1 0 0 0 0 0 0 1 img_002.jpg\n\n";
  std::ofstream(dir / "points3D.txt") << "# p\n";
  const ColmapModel m = readColmapText(dir);
  fs::remove_all(dir);
  EXPECT_EQ(m.camera_model, "SIMPLE_RADIAL");
  EXPECT_DOUBLE_EQ(m.camera.k1, -0.002);
  EXPECT_DOUBLE_EQ(m.camera.k2, 0.0);
  ASSERT_EQ(m.images.size(), 2u);
  EXPECT_EQ(m.images[0].image_id, 3);  // ordenadas por id
  EXPECT_DOUBLE_EQ(m.images[1].points2d[0].x, 10.0);
  const Reconstruction rec = toReconstruction(m, {{"img_009.jpg", 9}, {"img_002.jpg", 2}});
  ASSERT_TRUE(rec.cameras.count(9));
  EXPECT_TRUE(rec.cameras.at(9).registered);
  EXPECT_DOUBLE_EQ(rec.cameras.at(9).pose.t[0], 0.5);
  EXPECT_DOUBLE_EQ(rec.camera.cx, 2000.0);
}

TEST(ColmapIo, MalformedModelThrows) {
  const fs::path dir = tempDir("escaner3d_colmap_");
  std::ofstream(dir / "cameras.txt") << "1 OPENCV_FISHEYE 10 10 1 2 3 4 5 6 7 8\n";
  std::ofstream(dir / "images.txt") << "";
  std::ofstream(dir / "points3D.txt") << "";
  EXPECT_THROW(readColmapText(dir), ColmapFormatError);
  fs::remove_all(dir);
  EXPECT_THROW(readColmapText(dir), ColmapFormatError);  // no existe
}

// ---------- Ejecutor de procesos (con el ejecutable falso) ----------

TEST(ColmapRunner, CommandLineQuotesArguments) {
  EXPECT_EQ(quoteArgument("simple"), "simple");
  EXPECT_EQ(quoteArgument("con espacio"), "\"con espacio\"");
  EXPECT_EQ(commandLine("C:/a b/colmap.exe", {"mapper", "--x", "1"}), "\"C:/a b/colmap.exe\" mapper --x 1");
}

TEST(ColmapRunner, CapturesOutputArgumentsAndExitCode) {
  const fs::path dir = tempDir("escaner3d_stub_");
  setEnv("COLMAP_STUB_LOG", (dir / "args.txt").string());
  setEnv("COLMAP_STUB_OUTPUT", "hola desde el stub");
  setEnv("COLMAP_STUB_EXIT", "0");
  ColmapRunner colmap(COLMAP_STUB_PATH, dir / "colmap.log");
  const ProcessResult r = colmap.run("mapper", {{"--database_path", (dir / "a b.db").string()}, {"--x", "1"}});
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.output.find("hola desde el stub"), std::string::npos);
  EXPECT_GE(r.ms, 0.0);
  const std::string args = readAll(dir / "args.txt");
  EXPECT_NE(args.find("mapper\n--database_path\n" + (dir / "a b.db").string() + "\n--x\n1\n---"), std::string::npos);
  EXPECT_NE(readAll(dir / "colmap.log").find("mapper"), std::string::npos);

  setEnv("COLMAP_STUB_EXIT", "3");
  try {
    colmap.run("mapper", {});
    FAIL() << "debía lanzar ColmapError";
  } catch (const ColmapError& e) {
    EXPECT_NE(std::string(e.what()).find("3"), std::string::npos);
  }
  setEnv("COLMAP_STUB_EXIT", "0");
  setEnv("COLMAP_STUB_OUTPUT", "");
  fs::remove_all(dir);
}

TEST(ColmapRunner, MissingExecutableFailsClearly) {
  EXPECT_THROW(runProcess("C:/no/existe/colmap.exe", {"-h"}), ColmapError);
  EXPECT_THROW(findColmap("C:/no/existe/colmap.exe"), ColmapError);
  EXPECT_EQ(findColmap(COLMAP_STUB_PATH), fs::path(COLMAP_STUB_PATH));
}

TEST(ColmapRunner, DetectsCudaFromHelp) {
  EXPECT_TRUE(colmapHasCuda("COLMAP 4.2.1 (Commit bd1fcf6 on 2026-09-29 with CUDA, boost hash maps)\nUsage:"));
  EXPECT_FALSE(colmapHasCuda("COLMAP 4.2.1 (Commit bd1fcf6 on 2026-09-29 without CUDA)"));
  EXPECT_FALSE(colmapHasCuda("algo sin cabecera"));
  EXPECT_EQ(colmapVersion("COLMAP 4.2.1 (Commit bd1fcf6 with CUDA)\nUsage"), "COLMAP 4.2.1 (Commit bd1fcf6 with CUDA)");
  ColmapRunner colmap(COLMAP_STUB_PATH, fs::temp_directory_path() / "escaner3d_stub_help.log");
  EXPECT_TRUE(colmapHasCuda(colmap.help().output));
}

// ---------- Densificación (HU-19) con el ejecutable falso ----------

TEST(Dense, RunsUndistorterPatchMatchAndFusionWithParams) {
  const fs::path dir = tempDir("escaner3d_dense_");
  setEnv("COLMAP_STUB_LOG", (dir / "args.txt").string());
  ColmapRunner colmap(COLMAP_STUB_PATH, dir / "colmap.log");
  DenseParams p;
  p.max_image_size = 1200;
  const DenseResult r = runDensification(colmap, dir / "fotos", dir / "sparse", dir / "dense", p);
  EXPECT_EQ(r.fused_ply, dir / "dense" / "fused.ply");
  EXPECT_EQ(r.dense_points, 3);  // el PLY que deja el stub
  const std::string args = readAll(dir / "args.txt");
  const std::size_t u = args.find("image_undistorter");
  const std::size_t pm = args.find("patch_match_stereo");
  const std::size_t sf = args.find("stereo_fusion");
  ASSERT_NE(u, std::string::npos);
  ASSERT_NE(pm, std::string::npos);
  ASSERT_NE(sf, std::string::npos);
  EXPECT_LT(u, pm);
  EXPECT_LT(pm, sf);
  EXPECT_NE(args.find("--max_image_size\n1200"), std::string::npos);
  EXPECT_NE(args.find("--PatchMatchStereo.geom_consistency\n1"), std::string::npos);
  EXPECT_NE(args.find("--PatchMatchStereo.max_image_size\n1200"), std::string::npos);
  EXPECT_NE(args.find("--input_type\ngeometric"), std::string::npos);
  EXPECT_NE(args.find("--output_type\nCOLMAP"), std::string::npos);
  fs::remove_all(dir);
}

TEST(Dense, WithoutGeometricConsistencyFusesPhotometric) {
  const fs::path dir = tempDir("escaner3d_dense_");
  setEnv("COLMAP_STUB_LOG", (dir / "args.txt").string());
  ColmapRunner colmap(COLMAP_STUB_PATH, dir / "colmap.log");
  DenseParams p;
  p.geom_consistency = false;
  runDensification(colmap, dir / "fotos", dir / "sparse", dir / "dense", p);
  const std::string args = readAll(dir / "args.txt");
  EXPECT_NE(args.find("--PatchMatchStereo.geom_consistency\n0"), std::string::npos);
  EXPECT_NE(args.find("--input_type\nphotometric"), std::string::npos);
  fs::remove_all(dir);
}

TEST(Dense, FailingStepThrows) {
  const fs::path dir = tempDir("escaner3d_dense_");
  setEnv("COLMAP_STUB_EXIT", "1");
  ColmapRunner colmap(COLMAP_STUB_PATH, dir / "colmap.log");
  EXPECT_THROW(runDensification(colmap, dir, dir / "sparse", dir / "dense", DenseParams{}), ColmapError);
  setEnv("COLMAP_STUB_EXIT", "0");
  fs::remove_all(dir);
}

TEST(Dense, PlyVertexCount) {
  const fs::path dir = tempDir("escaner3d_ply_");
  std::ofstream(dir / "a.ply") << "ply\nformat binary_little_endian 1.0\nelement vertex 12345\nproperty float x\nend_header\n";
  std::ofstream(dir / "b.txt") << "no es ply\n";
  EXPECT_EQ(plyVertexCount(dir / "a.ply"), 12345);
  EXPECT_EQ(plyVertexCount(dir / "b.txt"), -1);
  EXPECT_EQ(plyVertexCount(dir / "no_existe.ply"), -1);
  fs::remove_all(dir);
}
