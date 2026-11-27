// Pruebas de HU-21: mejor cámara por triángulo, oclusión, atlas y UV.
#include "test_meshes.h"
#include "test_scene.h"
#include "texturing.h"

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <map>

namespace {

// Cámaras de 1000x750 (f = 900) que miran al cubo unitario: A desde arriba (+z), B desde +x.
CameraModel cam() { return scene::camera(); }

std::map<int, Pose> twoCameras() {
  return {{1, scene::lookAt({0.5, 0.52, 4.0}, {0.5, 0.5, 0.5})},   // casi vertical sobre la cara z = 1
          {2, scene::lookAt({4.0, 0.5, 0.52}, {0.5, 0.5, 0.5})}};  // de frente a la cara x = 1
}

std::map<int, cv::Mat> depthMaps(const Mesh& m, const std::map<int, Pose>& poses, int w, int h) {
  CameraModel small = cam();
  const double s = static_cast<double>(w) / small.width;
  small.f *= s, small.cx *= s, small.cy *= s, small.width = w, small.height = h;
  std::map<int, cv::Mat> d;
  for (const auto& [i, p] : poses) d[i] = renderDepth(m, small, p, w, h);
  return d;
}

// Imagen sintética: el color codifica la posición del píxel (para comprobar el muestreo).
cv::Mat gradientImage(int salt) {
  cv::Mat img(750, 1000, CV_8UC3);
  for (int y = 0; y < img.rows; ++y)
    for (int x = 0; x < img.cols; ++x)
      img.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<uchar>(x / 4), static_cast<uchar>(y / 3), static_cast<uchar>(salt));
  return img;
}

int faceIndexWithNormal(const Mesh& m, const cv::Vec3f& n) {
  const std::vector<cv::Vec3f> normals = faceNormals(m);
  for (std::size_t k = 0; k < normals.size(); ++k)
    if (cv::norm(normals[k] - n) < 1e-4) return static_cast<int>(k);
  return -1;
}

}  // namespace

TEST(Texturing, FaceNormalsPointOutOfCube) {
  const std::vector<cv::Vec3f> n = faceNormals(testmesh::cube());
  ASSERT_EQ(n.size(), 12u);
  EXPECT_LT(cv::norm(n[2] - cv::Vec3f(0, 0, 1)), 1e-6);  // cara z = 1
  EXPECT_LT(cv::norm(n[0] - cv::Vec3f(0, 0, -1)), 1e-6);
}

TEST(Texturing, DepthMapKeepsNearestSurface) {
  const Mesh m = testmesh::cube();
  const Pose top = scene::lookAt({0.5, 0.52, 4.0}, {0.5, 0.5, 0.5});
  const cv::Mat d = renderDepth(m, cam(), top, 1000, 750);
  ASSERT_EQ(d.type(), CV_32F);
  // En el centro se ve la cara de arriba (z = 1), a ~3 de la cámara, no la de abajo (~4).
  EXPECT_NEAR(d.at<float>(375, 500), 3.0f, 0.05f);
  EXPECT_TRUE(std::isinf(d.at<float>(5, 5)));
}

TEST(Texturing, EachFaceChoosesTheCameraThatSeesItFrontally) {
  const Mesh m = testmesh::cube();
  const auto poses = twoCameras();
  const std::vector<int> chosen = chooseCameras(m, cam(), poses, depthMaps(m, poses, 500, 375), TextureParams{});
  ASSERT_EQ(chosen.size(), 12u);
  EXPECT_EQ(chosen[faceIndexWithNormal(m, {0, 0, 1})], 1);   // arriba -> A
  EXPECT_EQ(chosen[faceIndexWithNormal(m, {1, 0, 0})], 2);   // +x -> B
  EXPECT_EQ(chosen[faceIndexWithNormal(m, {0, 0, -1})], -1); // abajo: nadie la ve
  EXPECT_EQ(chosen[faceIndexWithNormal(m, {0, -1, 0})], -1); // de canto para ambas (cos < 0,2)
}

TEST(Texturing, OccludedFaceDoesNotChooseThatCamera) {
  // Un cuadrado chico a z = 1 tapa el centro de un cuadrado grande a z = 0.
  Mesh m;
  m.vertices = {{-1, -1, 0}, {2, -1, 0}, {2, 2, 0}, {-1, 2, 0},                     // grande, abajo
                {0.3f, 0.3f, 1}, {0.7f, 0.3f, 1}, {0.7f, 0.7f, 1}, {0.3f, 0.7f, 1}, // tapa
                {0.45f, 0.45f, 0}, {0.55f, 0.45f, 0}, {0.5f, 0.55f, 0}};            // triángulo bajo la tapa
  m.faces = {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}, {8, 9, 10}};
  const std::map<int, Pose> poses = {{1, scene::lookAt({0.5, 0.52, 4.0}, {0.5, 0.5, 0.0})}};
  const std::vector<int> chosen = chooseCameras(m, cam(), poses, depthMaps(m, poses, 500, 375), TextureParams{});
  EXPECT_EQ(chosen[2], 1);   // la tapa se ve
  EXPECT_EQ(chosen[4], -1);  // el triángulo de abajo está tapado
}

TEST(Texturing, PackShelvesHasNoOverlaps) {
  std::vector<cv::Size> sizes;
  for (int k = 0; k < 60; ++k) sizes.emplace_back(10 + (k * 37) % 90, 8 + (k * 53) % 70);
  int used = 0;
  const std::vector<cv::Point> pos = packShelves(sizes, 512, used);
  ASSERT_EQ(pos.size(), sizes.size());
  for (std::size_t a = 0; a < sizes.size(); ++a) {
    const cv::Rect ra(pos[a], sizes[a]);
    EXPECT_GE(ra.x, 0);
    EXPECT_LE(ra.x + ra.width, 512);
    EXPECT_LE(ra.y + ra.height, used);
    for (std::size_t b = a + 1; b < sizes.size(); ++b) EXPECT_EQ((ra & cv::Rect(pos[b], sizes[b])).area(), 0);
  }
  int unused = 0;
  EXPECT_TRUE(packShelves({cv::Size(600, 10)}, 512, unused).empty());  // más ancho que el atlas
}

TEST(Texturing, AtlasUvAreInRangeAndColorsMatchTheImage) {
  const Mesh m = testmesh::cube();
  const auto poses = twoCameras();
  TextureParams p;
  p.depth_map_long_side = 500;
  const TextureResult r = textureMesh(m, cam(), poses, [](int i) { return gradientImage(i * 100); }, p);
  ASSERT_EQ(r.face_uv.size(), 12u);
  for (const auto& uv : r.face_uv)
    for (const cv::Vec2f& t : uv) {
      EXPECT_GE(t[0], 0.0f);
      EXPECT_LE(t[0], 1.0f);
      EXPECT_GE(t[1], 0.0f);
      EXPECT_LE(t[1], 1.0f);
    }
  EXPECT_EQ(r.stats.faces, 12);
  EXPECT_EQ(r.stats.textured_faces, 4);  // 2 caras de arriba y 2 de +x
  EXPECT_NEAR(r.stats.textured_pct, 100.0 * 4 / 12, 1e-9);
  EXPECT_EQ(r.stats.faces_per_camera.at(1), 2);
  EXPECT_EQ(r.stats.faces_per_camera.at(2), 2);
  EXPECT_DOUBLE_EQ(r.stats.chart_scale, 1.0);

  // El color del atlas en el baricentro de una cara de arriba = el de la imagen en su proyección.
  const int f = faceIndexWithNormal(m, {0, 0, 1});
  const cv::Vec3i& face = m.faces[static_cast<std::size_t>(f)];
  cv::Vec3d X(0, 0, 0);
  cv::Vec2f uv(0, 0);
  for (int k = 0; k < 3; ++k) {
    X += cv::Vec3d(m.vertices[static_cast<std::size_t>(face[k])]) / 3.0;
    uv += r.face_uv[static_cast<std::size_t>(f)][k] / 3.0f;
  }
  const cv::Point2d px = cam().project(poses.at(1), X);
  const cv::Vec3b expected = gradientImage(100).at<cv::Vec3b>(cvRound(px.y), cvRound(px.x));
  const int ax = static_cast<int>(uv[0] * r.atlas.cols);
  const int ay = static_cast<int>((1.0f - uv[1]) * r.atlas.rows);
  const cv::Vec3b got = r.atlas.at<cv::Vec3b>(ay, ax);
  for (int c = 0; c < 3; ++c) EXPECT_NEAR(got[c], expected[c], 6) << "canal " << c;
}

TEST(Texturing, ChartsAreScaledUniformlyWhenTheyDoNotFit) {
  const Mesh m = testmesh::cube();
  TextureParams p;
  p.atlas_max_size = 256;  // las cartas de 1000x750 no caben
  p.depth_map_long_side = 500;
  const TextureResult r = textureMesh(m, cam(), twoCameras(), [](int i) { return gradientImage(i); }, p);
  EXPECT_LT(r.stats.chart_scale, 1.0);
  EXPECT_LE(r.atlas.cols, 256);
  EXPECT_LE(r.atlas.rows, 256);
  EXPECT_EQ(r.stats.textured_faces, 4);
}

TEST(Texturing, UndistortMovesPixelsAccordingToModel) {
  CameraModel c = cam();
  c.k1 = -0.2;
  cv::Mat img(750, 1000, CV_8UC3, cv::Scalar(0, 0, 0));
  // Punto distorsionado de un punto normalizado conocido (a, b) = (0,3, 0,2).
  const cv::Point2d d = c.projectCamera({0.3, 0.2, 1.0});
  cv::circle(img, cv::Point(cvRound(d.x), cvRound(d.y)), 3, cv::Scalar(255, 255, 255), -1);
  const cv::Mat und = undistortImage(img, c);
  // Sin distorsión, el punto va a f·a + cx, f·b + cy.
  EXPECT_GT(und.at<cv::Vec3b>(cvRound(900 * 0.2 + 375), cvRound(900 * 0.3 + 500))[0], 100);
}
