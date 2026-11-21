// Pruebas de HU-17: frustum y filtrado de puntos.
#include "test_sfm_scene.h"
#include "visibility.h"

#include <gtest/gtest.h>

namespace {

// Cámara en el origen mirando hacia +z (pose identidad).
const Pose kIdentity{};

// Punto en coordenadas de cámara que se proyecta exactamente en el píxel (u, v) a profundidad z.
cv::Vec3d atPixel(const CameraModel& c, double u, double v, double z = 5.0) {
  return {(u - c.cx) / c.f * z, (v - c.cy) / c.f * z, z};
}

// Reconstrucción con 4 cámaras registradas en un anillo y un punto observado por `views`.
Reconstruction withPoint(const std::vector<int>& views, const cv::Vec3d& X = {0.1, 0.2, 0.1}) {
  Reconstruction rec;
  rec.camera = scene::camera();
  const std::vector<Pose> poses = scene::ring(4, 6.0, 30.0);
  for (int k = 0; k < 4; ++k) {
    Camera& c = rec.cameras[k + 1];
    c.index = k + 1;
    c.registered = true;
    c.pose = poses[static_cast<std::size_t>(k)];
  }
  Track t;
  Point3D p;
  p.xyz = X;
  p.track = 0;
  for (int v : views) {
    const Observation o{v, 7, rec.camera.project(rec.cameras[v].pose, X)};
    p.observations.push_back(o);
    t.observations.push_back(o);
  }
  rec.tracks = {t};
  rec.points = {p};
  rec.reindexTracks();
  return rec;
}

}  // namespace

TEST(Visibility, FrustumInsideOutsideBehindAndBorder) {
  const CameraModel c = scene::camera();  // 1000 x 750
  EXPECT_TRUE(isVisible(c, kIdentity, atPixel(c, 500, 375)));
  EXPECT_FALSE(isVisible(c, kIdentity, atPixel(c, -3, 375)));     // fuera por la izquierda
  EXPECT_FALSE(isVisible(c, kIdentity, atPixel(c, 1003, 375)));   // por la derecha
  EXPECT_FALSE(isVisible(c, kIdentity, atPixel(c, 500, 760)));    // por abajo
  EXPECT_FALSE(isVisible(c, kIdentity, {0.0, 0.0, -2.0}));        // detrás
  EXPECT_FALSE(isVisible(c, kIdentity, {0.0, 0.0, 0.0}));         // en el centro óptico
  // Borde: la imagen ocupa [-0,5, 999,5] x [-0,5, 749,5].
  EXPECT_TRUE(isVisible(c, kIdentity, atPixel(c, 999.5, 749.5)));
  EXPECT_TRUE(isVisible(c, kIdentity, atPixel(c, -0.5, -0.5)));
  EXPECT_FALSE(isVisible(c, kIdentity, atPixel(c, 999.5 + 1e-6, 300)));
  EXPECT_FALSE(isVisible(c, kIdentity, atPixel(c, 300, -0.5 - 1e-6)));
}

TEST(Visibility, TwoViewPointIsRemovedThreeViewPointKept) {
  Reconstruction two = withPoint({1, 2});
  const FilterStats s = filterPoints(two, 3, 2.0);
  EXPECT_TRUE(two.points.empty());
  EXPECT_EQ(s.points_few_views, 1);
  EXPECT_EQ(two.point_of_track[0], -1);

  Reconstruction three = withPoint({1, 2, 3});
  filterPoints(three, 3, 2.0);
  ASSERT_EQ(three.points.size(), 1u);
  EXPECT_EQ(three.point_of_track[0], 0);
}

TEST(Visibility, ObservationBehindCameraDoesNotCount) {
  Reconstruction rec = withPoint({1, 2, 3});
  // Se mueve la cámara 3 al otro lado del punto, mirando hacia afuera.
  const cv::Vec3d X = rec.points[0].xyz;
  rec.cameras[3].pose = scene::lookAt(X + cv::Vec3d(0, 0, -1), X + cv::Vec3d(0, 0, -5));
  const FilterStats s = filterPoints(rec, 3, 1e9);
  EXPECT_TRUE(rec.points.empty());
  EXPECT_EQ(s.observations_not_visible, 1);
}

TEST(Visibility, ObservationOutsideImageDoesNotCount) {
  Reconstruction rec = withPoint({1, 2, 3});
  rec.camera.width = 10;  // imagen diminuta: el punto cae fuera en todas las cámaras
  rec.camera.height = 10;
  const FilterStats s = filterPoints(rec, 3, 1e9);
  EXPECT_TRUE(rec.points.empty());
  EXPECT_EQ(s.observations_not_visible, 3);
}

TEST(Visibility, HighErrorObservationsAreRemoved) {
  Reconstruction rec = withPoint({1, 2, 3, 4});
  rec.points[0].observations[1].xy.x += 10;  // 10 px de error
  FilterStats s = filterPoints(rec, 3, 2.0);
  ASSERT_EQ(rec.points.size(), 1u);
  EXPECT_EQ(rec.points[0].observations.size(), 3u);
  EXPECT_EQ(s.observations_high_error, 1);

  Reconstruction bad = withPoint({1, 2, 3, 4});
  bad.points[0].observations[1].xy.x += 10;
  bad.points[0].observations[2].xy.y -= 10;
  s = filterPoints(bad, 3, 2.0);
  EXPECT_TRUE(bad.points.empty());
  EXPECT_EQ(s.observations_high_error, 2);
}

TEST(Visibility, MinViewsIsRespected) {
  Reconstruction rec = withPoint({1, 2, 3});
  filterPoints(rec, 4, 2.0);
  EXPECT_TRUE(rec.points.empty());
  Reconstruction rec2 = withPoint({1, 2});
  filterPoints(rec2, 2, 2.0);
  EXPECT_EQ(rec2.points.size(), 1u);
}

TEST(Visibility, PipelineKeepsOnlyFilteredPoints) {
  const scene::SfmScene s = scene::makeRingScene();
  ReconstructionParams p;
  const SfmResult r = runIncrementalSfm(s.input, p);
  ASSERT_GT(r.rec.points.size(), 100u);
  EXPECT_GE(r.filter_final.points_before, r.filter_final.points_after);
  EXPECT_EQ(r.ba_runs.back().stage, "final_2");
  for (const Point3D& pt : r.rec.points) {
    ASSERT_GE(static_cast<int>(pt.observations.size()), p.min_views);
    for (const Observation& o : pt.observations) {
      const Pose& pose = r.rec.cameras.at(o.image).pose;
      EXPECT_TRUE(isVisible(r.rec.camera, pose, pt.xyz));
      EXPECT_LE(reprojectionError(r.rec.camera, pose, pt.xyz, o.xy), p.max_reproj_error_px);
    }
  }
}
