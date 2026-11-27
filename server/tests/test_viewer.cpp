// Pruebas de HU-23: proyección, órbita, z-buffer, renderizado y decimación del visor.
#include "test_meshes.h"
#include "viewer.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

// Cubo con textura roja uniforme.
RenderModel redCube() {
  RenderModel m;
  m.mesh = testmesh::cube();
  m.texture = cv::Mat(8, 8, CV_8UC3, cv::Scalar(0, 0, 255));
  m.face_uv.assign(m.mesh.faces.size(), {cv::Vec2f(0.2f, 0.2f), cv::Vec2f(0.8f, 0.2f), cv::Vec2f(0.5f, 0.8f)});
  return m;
}

}  // namespace

TEST(Viewer, OrbitPoseLooksAtTargetFromSphericalPosition) {
  ViewCamera v;
  v.target = {1, 2, 3};
  v.yaw_deg = 90;
  v.pitch_deg = 0;
  v.distance = 5;
  const Pose p = orbitPose(v);
  EXPECT_LT(cv::norm(p.center() - cv::Vec3d(1, 7, 3)), 1e-9);  // yaw 90°: sobre +y
  const cv::Vec3d t = p.toCamera(v.target);
  EXPECT_NEAR(t[0], 0, 1e-9);
  EXPECT_NEAR(t[1], 0, 1e-9);
  EXPECT_NEAR(t[2], 5, 1e-9);
}

TEST(Viewer, ProjectionHandComputed) {
  ViewCamera v;
  v.yaw_deg = 0;
  v.pitch_deg = 0;
  v.distance = 4;
  v.fov_deg = 90;  // f = (alto/2) / tan(45°) = alto/2
  const cv::Point2d c = projectView(v, 640, 480, {0, 0, 0});
  EXPECT_NEAR(c.x, 319.5, 1e-9);
  EXPECT_NEAR(c.y, 239.5, 1e-9);
  // Cámara en +x mirando a -x: el eje z del mundo (arriba) va hacia arriba en la imagen.
  const cv::Point2d up = projectView(v, 640, 480, {0, 0, 1});
  EXPECT_NEAR(up.x, 319.5, 1e-9);
  EXPECT_NEAR(up.y, 239.5 - 240.0 * 1.0 / 4.0, 1e-9);
}

TEST(Viewer, ZBufferKeepsNearestPoint) {
  RenderModel m;
  m.points = {{0, 0, 0}, {1, 0, 0}};  // el (1,0,0) está más cerca de una cámara en +x
  m.point_colors = {cv::Vec3b(0, 255, 0), cv::Vec3b(255, 0, 0)};  // RGB: verde lejos, rojo cerca
  ViewCamera v;
  v.pitch_deg = 0;
  v.yaw_deg = 0;
  v.distance = 5;
  const cv::Mat img = renderView(m, v, 200, 200, 1000);
  const cv::Point2d p = projectView(v, 200, 200, {1, 0, 0});
  const cv::Vec3b bgr = img.at<cv::Vec3b>(cvRound(p.y), cvRound(p.x));
  EXPECT_EQ(bgr, cv::Vec3b(0, 0, 255));  // rojo (BGR)
}

TEST(Viewer, RenderedCubeShowsTextureAtFaceProjection) {
  const RenderModel m = redCube();
  ViewCamera v = fitView(m);
  v.yaw_deg = 30;
  v.pitch_deg = 25;
  const cv::Mat img = renderView(m, v, 320, 240, 100000);
  ASSERT_EQ(img.size(), cv::Size(320, 240));
  // Centro de la cara de arriba (z = 1): debe verse rojo.
  const cv::Point2d p = projectView(v, 320, 240, {0.5, 0.5, 1.0});
  const cv::Vec3b c = img.at<cv::Vec3b>(cvRound(p.y), cvRound(p.x));
  EXPECT_GT(c[2], 150);
  EXPECT_LT(c[1], 80);
  // Una esquina de la imagen queda con el fondo.
  EXPECT_NE(img.at<cv::Vec3b>(2, 2), c);
}

TEST(Viewer, TwoViewsOutputHasTheRequestedSize) {
  const RenderModel m = redCube();
  const ViewCamera g = fitView(m);
  const cv::Mat both = renderTwoViews(m, g, detailView(g, {0.5, 0.5, 1.0}), 640, 240, 1000);
  EXPECT_EQ(both.cols, 640);
  EXPECT_EQ(both.rows, 240);
  const cv::Mat left = both(cv::Rect(0, 0, 320, 240)), right = both(cv::Rect(320, 0, 320, 240));
  EXPECT_GT(cv::norm(left, right, cv::NORM_L1), 0.0);  // vistas distintas
}

TEST(Viewer, DetailViewIsCloserAndCentered) {
  ViewCamera g;
  g.distance = 10;
  const ViewCamera d = detailView(g, {1, 1, 1}, 4.0);
  EXPECT_NEAR(d.distance, 2.5, 1e-12);
  EXPECT_EQ(d.target, cv::Vec3d(1, 1, 1));
  EXPECT_DOUBLE_EQ(d.yaw_deg, g.yaw_deg);
}

TEST(Viewer, DecimationRespectsMaximum) {
  EXPECT_EQ(decimateFaces(100, 1000).size(), 100u);
  const std::vector<int> d = decimateFaces(100000, 150);
  EXPECT_LE(d.size(), 150u);
  EXPECT_GE(d.size(), 140u);
  for (std::size_t k = 1; k < d.size(); ++k) EXPECT_GT(d[k], d[k - 1]);
}

TEST(Viewer, FitViewFramesTheModel) {
  const ViewCamera v = fitView(redCube());
  EXPECT_LT(cv::norm(v.target - cv::Vec3d(0.5, 0.5, 0.5)), 1e-6);
  // Los 8 vértices caen dentro de la imagen.
  for (const cv::Vec3f& x : testmesh::cube().vertices) {
    const cv::Point2d p = projectView(v, 320, 240, cv::Vec3d(x));
    EXPECT_GE(p.x, 0);
    EXPECT_LT(p.x, 320);
    EXPECT_GE(p.y, 0);
    EXPECT_LT(p.y, 240);
  }
}
