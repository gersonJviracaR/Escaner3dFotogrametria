// Pruebas de HU-22: OBJ, MTL, textura y escala.
#include "obj_export.h"
#include "test_meshes.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace {

TextureResult plainTexture(const Mesh& m) {
  TextureResult t;
  t.face_camera.assign(m.faces.size(), 1);
  t.face_uv.assign(m.faces.size(), {cv::Vec2f(0.1f, 0.2f), cv::Vec2f(0.5f, 0.2f), cv::Vec2f(0.5f, 0.9f)});
  t.atlas = cv::Mat(16, 16, CV_8UC3, cv::Scalar(10, 20, 30));
  return t;
}

struct ParsedObj {
  std::vector<cv::Vec3d> v;
  int vt = 0, vn = 0;
  std::vector<std::vector<std::array<int, 3>>> faces;  // por cara: (v, vt, vn) base 1
  std::vector<std::string> comments;
  std::string mtllib, usemtl;
};

ParsedObj parse(const std::string& text) {
  ParsedObj o;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "v") {
      cv::Vec3d p;
      ss >> p[0] >> p[1] >> p[2];
      o.v.push_back(p);
    } else if (tag == "vt") {
      ++o.vt;
    } else if (tag == "vn") {
      ++o.vn;
    } else if (tag == "f") {
      std::vector<std::array<int, 3>> f;
      std::string corner;
      while (ss >> corner) {
        std::array<int, 3> idx{0, 0, 0};
        std::istringstream cs(corner);
        std::string part;
        for (int k = 0; k < 3 && std::getline(cs, part, '/'); ++k) idx[static_cast<std::size_t>(k)] = part.empty() ? 0 : std::stoi(part);
        f.push_back(idx);
      }
      o.faces.push_back(f);
    } else if (tag == "#") {
      o.comments.push_back(line);
    } else if (tag == "mtllib") {
      ss >> o.mtllib;
    } else if (tag == "usemtl") {
      ss >> o.usemtl;
    }
  }
  return o;
}

}  // namespace

TEST(ObjExport, FaceIndicesAreOneBasedAndInRange) {
  Mesh m = testmesh::cube();
  for (const cv::Vec3f& v : m.vertices) m.normals.push_back(cv::normalize(v - cv::Vec3f(0.5f, 0.5f, 0.5f)));
  const ParsedObj o = parse(objText(m, plainTexture(m), 1.0, "prueba"));
  EXPECT_EQ(o.v.size(), 8u);
  EXPECT_EQ(o.vt, 36);  // un vt por vértice de cara
  EXPECT_EQ(o.vn, 8);
  ASSERT_EQ(o.faces.size(), 12u);
  for (const auto& f : o.faces) {
    ASSERT_EQ(f.size(), 3u);
    for (const auto& c : f) {
      EXPECT_GE(c[0], 1);
      EXPECT_LE(c[0], 8);
      EXPECT_GE(c[1], 1);
      EXPECT_LE(c[1], 36);
      EXPECT_GE(c[2], 1);
      EXPECT_LE(c[2], 8);
    }
  }
  EXPECT_EQ(o.mtllib, "model.mtl");
  EXPECT_FALSE(o.usemtl.empty());
}

TEST(ObjExport, WithoutNormalsFacesHaveNoVn) {
  const Mesh m = testmesh::plane();
  const ParsedObj o = parse(objText(m, plainTexture(m), 1.0, ""));
  EXPECT_EQ(o.vn, 0);
  EXPECT_EQ(o.faces[0][0][2], 0);
}

TEST(ObjExport, MtlReferencesTextureWithRelativePath) {
  const std::string mtl = mtlText("texture.png");
  EXPECT_NE(mtl.find("newmtl"), std::string::npos);
  EXPECT_NE(mtl.find("map_Kd texture.png"), std::string::npos);
  EXPECT_EQ(mtl.find(":\\"), std::string::npos);  // nada de rutas absolutas
}

TEST(ObjExport, ScaleByReferencePointsMakesDistanceEqualD) {
  const ScaleSpec spec = parseScaleRef("0,0,0;1,0,0;250");
  EXPECT_EQ(spec.kind, ScaleSpec::Kind::Reference);
  EXPECT_DOUBLE_EQ(scaleFactor(spec), 250.0);
  const ScaleSpec spec2 = parseScaleRef("0.5, 0.5, 0 ; 0.5, 0.5, 1 ; 80");  // admite espacios
  const Mesh m = testmesh::cube();
  const ParsedObj o = parse(objText(m, plainTexture(m), scaleFactor(spec2), "escala"));
  // El vértice (0,0,0) y el (0,0,1) quedan a 80 mm: la distancia de los puntos de referencia.
  const double d = cv::norm(o.v[4] - o.v[0]);
  EXPECT_NEAR(d, 80.0, 80.0 * 1e-6);
}

TEST(ObjExport, DirectFactorAndNoScale) {
  EXPECT_DOUBLE_EQ(scaleFactor(scaleByFactor(3.5)), 3.5);
  EXPECT_DOUBLE_EQ(scaleFactor(ScaleSpec{}), 1.0);
  EXPECT_THROW(scaleByFactor(0), std::invalid_argument);
}

TEST(ObjExport, InvalidReferencesAreRejected) {
  EXPECT_THROW(parseScaleRef("1,2,3;1,2,3;100"), std::invalid_argument);  // distancia 0
  EXPECT_THROW(parseScaleRef("1,2,3;4,5,6;-5"), std::invalid_argument);
  EXPECT_THROW(parseScaleRef("1,2;4,5,6;10"), std::invalid_argument);
  EXPECT_THROW(parseScaleRef("a,b,c;4,5,6;10"), std::invalid_argument);
}

TEST(ObjExport, ReadObjRoundTripsGeometryAndUv) {
  const fs::path dir = fs::temp_directory_path() /
                       ("escaner3d_objrt_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const Mesh m = testmesh::cube();
  TextureResult t = plainTexture(m);
  t.face_uv[3] = {cv::Vec2f(0.25f, 0.5f), cv::Vec2f(0.75f, 0.5f), cv::Vec2f(0.5f, 1.0f)};
  exportObj(dir, m, t, scaleByFactor(2.0));
  const ObjModel back = readObj(dir / "model.obj");
  fs::remove_all(dir);
  ASSERT_EQ(back.mesh.vertices.size(), 8u);
  ASSERT_EQ(back.mesh.faces.size(), 12u);
  EXPECT_EQ(back.mesh.vertices[6], cv::Vec3f(2, 2, 2));  // escalado x2
  EXPECT_EQ(back.mesh.faces[5], m.faces[5]);
  EXPECT_EQ(back.face_uv[3][0], cv::Vec2f(0.25f, 0.5f));
  EXPECT_EQ(back.texture_file, "texture.png");
  EXPECT_THROW(readObj(dir / "no_existe.obj"), std::runtime_error);
}

TEST(ObjExport, WritesFilesAndReportsScaleUnitAndClosure) {
  const fs::path dir = fs::temp_directory_path() /
                       ("escaner3d_obj_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const Mesh m = testmesh::cube();
  const ObjExport scaled = exportObj(dir, m, plainTexture(m), parseScaleRef("0,0,0;0,0,1;50"));
  EXPECT_TRUE(fs::exists(dir / "model.obj"));
  EXPECT_TRUE(fs::exists(dir / "model.mtl"));
  EXPECT_TRUE(fs::exists(dir / "texture.png"));
  EXPECT_DOUBLE_EQ(scaled.scale, 50.0);
  EXPECT_EQ(scaled.unit, "mm");
  EXPECT_TRUE(scaled.closed);
  EXPECT_EQ(scaled.open_edges, 0);
  EXPECT_EQ(scaled.faces, 12);
  const ObjExport plain = exportObj(dir, testmesh::plane(), plainTexture(testmesh::plane()), ScaleSpec{});
  EXPECT_DOUBLE_EQ(plain.scale, 1.0);
  EXPECT_EQ(plain.unit, "arbitraria");
  EXPECT_FALSE(plain.closed);
  EXPECT_EQ(plain.open_edges, 4);
  fs::remove_all(dir);
}
