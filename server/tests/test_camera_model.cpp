// Pruebas del modelo de cámara del Epic 3 (convenciones de la sección 1 del Sprint 3).
#include "camera_model.h"
#include "test_scene.h"

#include <ceres/rotation.h>
#include <gtest/gtest.h>

#include <cmath>

namespace {

Quat axisAngle(double deg, double ax, double ay, double az) {
  const double n = std::sqrt(ax * ax + ay * ay + az * az);
  const double h = deg * scene::kPi / 360.0;
  return {std::cos(h), std::sin(h) * ax / n, std::sin(h) * ay / n, std::sin(h) * az / n};
}

}  // namespace

TEST(CameraModel, QuaternionMatchesCeresConvention) {
  const Quat q = axisAngle(37, 0.3, -1, 0.5);
  const cv::Matx33d R = quaternionToRotation(q);
  double Rc[9];
  ceres::QuaternionToRotation(q.data(), Rc);  // fila mayor
  for (int k = 0; k < 9; ++k) EXPECT_NEAR(R.val[k], Rc[k], 1e-12);
  // Rotación de 90° alrededor de z: x -> y.
  const cv::Vec3d y = quaternionToRotation(axisAngle(90, 0, 0, 1)) * cv::Vec3d(1, 0, 0);
  EXPECT_NEAR(y[1], 1.0, 1e-12);
}

TEST(CameraModel, RotationQuaternionRoundTrip) {
  for (double deg : {0.0, 10.0, 90.0, 179.0, 180.0}) {
    const Quat q = axisAngle(deg, 1, 2, -2);
    const cv::Matx33d R = quaternionToRotation(q);
    const Quat back = rotationToQuaternion(R);
    EXPECT_NEAR(quaternionNorm(back), 1.0, 1e-12);
    EXPECT_GE(back[0], 0.0);
    EXPECT_LT(cv::norm(quaternionToRotation(back) - R), 1e-9) << deg;
  }
  EXPECT_THROW(quaternionToRotation({0, 0, 0, 0}), std::invalid_argument);
}

TEST(CameraModel, PoseIsWorldToCamera) {
  const cv::Vec3d C(3, -1, 2);
  const Pose p = scene::lookAt(C);
  EXPECT_LT(cv::norm(p.center() - C), 1e-12);         // C = -Rᵀt
  EXPECT_LT(cv::norm(p.toCamera(C)), 1e-12);          // el centro va al origen de la cámara
  EXPECT_GT(p.toCamera({0, 0, 0})[2], 0.0);           // el origen queda delante
  const cv::Matx34d P = p.matrix();
  const cv::Vec3d X(0.2, 0.3, -0.1);
  const cv::Vec3d viaP = P * cv::Vec4d(X[0], X[1], X[2], 1);
  EXPECT_LT(cv::norm(viaP - p.toCamera(X)), 1e-12);
}

TEST(CameraModel, ProjectionWithoutDistortionIsPinhole) {
  const CameraModel c = scene::camera();
  const cv::Point2d u = c.projectCamera({0.5, -0.25, 2.0});
  EXPECT_NEAR(u.x, 900 * 0.25 + 500, 1e-12);
  EXPECT_NEAR(u.y, 900 * -0.125 + 375, 1e-12);
}

TEST(CameraModel, ProjectionWithDistortionHandComputed) {
  CameraModel c = scene::camera();
  c.k1 = -0.1;
  c.k2 = 0.02;
  // a = 0.3, b = 0.4 -> r² = 0.25; d = 1 - 0.025 + 0.00125 = 0.97625
  const cv::Point2d u = c.projectCamera({0.3, 0.4, 1.0});
  EXPECT_NEAR(u.x, 900 * 0.97625 * 0.3 + 500, 1e-9);
  EXPECT_NEAR(u.y, 900 * 0.97625 * 0.4 + 375, 1e-9);
}

TEST(CameraModel, PixelToNormalizedInvertsDistortion) {
  CameraModel c = scene::camera();
  c.k1 = -0.12;
  c.k2 = 0.03;
  for (double a : {-0.5, -0.1, 0.0, 0.3, 0.55})
    for (double b : {-0.4, 0.0, 0.2}) {
      const cv::Point2d n = c.pixelToNormalized(c.projectCamera({a, b, 1.0}));
      EXPECT_NEAR(n.x, a, 1e-9);
      EXPECT_NEAR(n.y, b, 1e-9);
    }
}

TEST(CameraModel, ReprojectionErrorIsPixelDistance) {
  const CameraModel c = scene::camera();
  const Pose p = scene::lookAt({0, -5, 0});
  const cv::Vec3d X(0.1, 0.2, 0.3);
  const cv::Point2d u = c.project(p, X);
  EXPECT_NEAR(reprojectionError(c, p, X, {u.x + 3, u.y + 4}), 5.0, 1e-9);
}
