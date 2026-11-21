#include "visibility.h"

bool isVisible(const CameraModel& camera, const Pose& pose, const cv::Vec3d& X) {
  const cv::Vec3d x = pose.toCamera(X);
  if (!(x[2] > 0)) return false;
  const cv::Point2d u = camera.projectCamera(x);
  constexpr double kEps = 1e-9;  // redondeo de la proyección: el borde exacto cuenta como dentro
  return u.x >= -0.5 - kEps && u.y >= -0.5 - kEps && u.x <= camera.width - 0.5 + kEps &&
         u.y <= camera.height - 0.5 + kEps;
}

FilterStats filterPoints(Reconstruction& rec, int min_views, double max_error_px) {
  FilterStats s;
  s.points_before = static_cast<int>(rec.points.size());
  std::vector<Point3D> kept;
  kept.reserve(rec.points.size());
  for (Point3D& p : rec.points) {
    std::vector<Observation> good;
    for (const Observation& o : p.observations) {
      const auto it = rec.cameras.find(o.image);
      if (it == rec.cameras.end() || !it->second.registered || !isVisible(rec.camera, it->second.pose, p.xyz)) {
        ++s.observations_not_visible;
        continue;
      }
      if (reprojectionError(rec.camera, it->second.pose, p.xyz, o.xy) > max_error_px) {
        ++s.observations_high_error;
        continue;
      }
      good.push_back(o);
    }
    if (static_cast<int>(good.size()) < min_views) {
      ++s.points_few_views;
      continue;
    }
    p.observations = std::move(good);
    kept.push_back(std::move(p));
  }
  rec.points = std::move(kept);
  rec.reindexTracks();
  s.points_after = static_cast<int>(rec.points.size());
  return s;
}
