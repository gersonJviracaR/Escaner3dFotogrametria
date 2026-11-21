// Pruebas de HU-14: triangulación DLT en coordenadas homogéneas.
#include "test_scene.h"
#include "triangulation.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

std::vector<cv::Point2d> normalizedObservations(const CameraModel& c, const std::vector<Pose>& poses,
                                                const cv::Vec3d& X, cv::RNG& rng, double noise_px) {
  std::vector<cv::Point2d> obs;
  for (const Pose& p : poses) obs.push_back(c.pixelToNormalized(scene::observe(c, p, X, rng, noise_px)));
  return obs;
}

}  // namespace

TEST(Triangulation, NoiselessDLTRecoversPoint) {
  const CameraModel c = scene::camera();
  const std::vector<Pose> poses = scene::ring(3);
  cv::RNG rng(1);
  for (const cv::Vec3d& X : scene::randomPoints(50)) {
    const cv::Vec3d est = triangulateDLT(poses, normalizedObservations(c, poses, X, rng, 0.0));
    EXPECT_LT(cv::norm(est - X), 1e-6);
  }
}

TEST(Triangulation, HomogeneousSolutionIsUnitVector) {
  const CameraModel c = scene::camera();
  const std::vector<Pose> poses = scene::ring(2);
  cv::RNG rng(1);
  const cv::Vec4d Xh = triangulateHomogeneous(poses, normalizedObservations(c, poses, {0.1, 0.2, 0.3}, rng, 0.0));
  EXPECT_NEAR(cv::norm(Xh), 1.0, 1e-12);
  EXPECT_NEAR(Xh[0] / Xh[3], 0.1, 1e-6);
}

TEST(Triangulation, NoisyDLTErrorIsSmallAndImprovesWithMoreViews) {
  const CameraModel c = scene::camera();
  const std::vector<Pose> all = scene::ring(5, 6.0, 15.0);
  const std::vector<Pose> two(all.begin(), all.begin() + 2);
  cv::RNG rng(3);
  double err2 = 0, err5 = 0;
  const std::vector<cv::Vec3d> pts = scene::randomPoints(300, 11);
  for (const cv::Vec3d& X : pts) {
    err2 += cv::norm(triangulateDLT(two, normalizedObservations(c, two, X, rng, 0.3)) - X);
    err5 += cv::norm(triangulateDLT(all, normalizedObservations(c, all, X, rng, 0.3)) - X);
  }
  err2 /= pts.size();
  err5 /= pts.size();
  EXPECT_LT(err2, 0.05);  // a ~6 de distancia, con 15° de separación
  EXPECT_LT(err5, err2);
}

TEST(Triangulation, AngleHandComputed) {
  EXPECT_NEAR(triangulationAngleDeg({0, 0, 0}, {1, 0, 0}, {0, 1, 0}), 90.0, 1e-12);
  EXPECT_NEAR(triangulationAngleDeg({0, 0, 0}, {1, 0, 0}, {1, 1, 0}), 45.0, 1e-12);
  EXPECT_NEAR(triangulationAngleDeg({0, 0, 0}, {2, 0, 0}, {5, 0, 0}), 0.0, 1e-6);
  EXPECT_NEAR(maxTriangulationAngleDeg({0, 0, 0}, {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}}), 90.0, 1e-12);
}

TEST(Triangulation, CheckedAcceptsGoodPoint) {
  const CameraModel c = scene::camera();
  const std::vector<Pose> poses = scene::ring(2);
  cv::RNG rng(5);
  const cv::Vec3d X(0.2, -0.1, 0.4);
  const std::vector<cv::Point2d> px = {scene::observe(c, poses[0], X, rng, 0.3),
                                       scene::observe(c, poses[1], X, rng, 0.3)};
  const TriangulationResult r = triangulateChecked(c, poses, px, 1.5, 2.0);
  EXPECT_TRUE(r.ok) << r.reason;
  EXPECT_NEAR(r.angle_deg, 30.0, 3.0);
  EXPECT_LT(r.max_error_px, 2.0);
  EXPECT_LT(cv::norm(r.X - X), 0.05);
}

TEST(Triangulation, PointBehindACameraIsDiscarded) {
  const CameraModel c = scene::camera();
  // Dos cámaras en línea mirando en la misma dirección; el punto queda detrás de ambas.
  const Pose a = scene::lookAt({0, -5, 0}, {0, 0, 0});
  const Pose b = scene::lookAt({1, -5, 0}, {1, 0, 0});
  const cv::Vec3d X(0.5, -8, 0.2);  // detrás (y < -5)
  // Las proyecciones de un punto detrás de la cámara son las del punto reflejado en el centro.
  const std::vector<cv::Point2d> px = {c.projectCamera(-a.toCamera(X)), c.projectCamera(-b.toCamera(X))};
  const TriangulationResult r = triangulateChecked(c, {a, b}, px, 1.5, 2.0);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.reason, "detras_de_camara");
}

TEST(Triangulation, SmallTriangulationAngleIsDiscarded) {
  const CameraModel c = scene::camera();
  const Pose a = scene::lookAt({0, -6, 0});
  const Pose b = scene::lookAt({0.05, -6, 0});  // línea base casi nula: < 0,5°
  cv::RNG rng(9);
  const cv::Vec3d X(0.1, 0.0, 0.1);
  const std::vector<cv::Point2d> px = {scene::observe(c, a, X, rng, 0.0), scene::observe(c, b, X, rng, 0.0)};
  const TriangulationResult r = triangulateChecked(c, {a, b}, px, 1.5, 2.0);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.reason, "angulo_pequeno");
  EXPECT_LT(r.angle_deg, 1.5);
}

TEST(Triangulation, InconsistentObservationsHaveHighError) {
  const CameraModel c = scene::camera();
  const std::vector<Pose> poses = scene::ring(3);
  cv::RNG rng(2);
  const cv::Vec3d X(0.1, 0.1, 0.1);
  std::vector<cv::Point2d> px;
  for (const Pose& p : poses) px.push_back(scene::observe(c, p, X, rng, 0.0));
  px[2].x += 25;  // una vista muy desplazada
  const TriangulationResult r = triangulateChecked(c, poses, px, 1.5, 2.0);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.reason, "error_alto");
}

TEST(Triangulation, UsesDistortionModel) {
  CameraModel c = scene::camera();
  c.k1 = -0.15;
  c.k2 = 0.05;
  const std::vector<Pose> poses = scene::ring(2);
  cv::RNG rng(4);
  const cv::Vec3d X(0.6, -0.5, 0.7);
  const std::vector<cv::Point2d> px = {scene::observe(c, poses[0], X, rng, 0.0),
                                       scene::observe(c, poses[1], X, rng, 0.0)};
  const TriangulationResult r = triangulateChecked(c, poses, px, 1.5, 2.0);
  ASSERT_TRUE(r.ok) << r.reason;
  EXPECT_LT(cv::norm(r.X - X), 1e-6);
}
