#include "registration.h"

#include "triangulation.h"

#include <opencv2/calib3d.hpp>

#include <algorithm>

Correspondences2D3D correspondences2D3D(const Reconstruction& rec, int image) {
  Correspondences2D3D c;
  for (std::size_t t = 0; t < rec.tracks.size(); ++t) {
    const int pid = rec.point_of_track[t];
    if (pid < 0) continue;
    for (const Observation& o : rec.tracks[t].observations)
      if (o.image == image) {
        c.points.push_back(pid);
        c.observations.push_back(o);
        break;
      }
  }
  return c;
}

RegistrationResult registerCamera(Reconstruction& rec, int image, const ReconstructionParams& params) {
  RegistrationResult r;
  const Correspondences2D3D c = correspondences2D3D(rec, image);
  r.n_correspondences = static_cast<int>(c.points.size());
  if (r.n_correspondences < std::max(params.pnp_min_inliers, 6)) {
    r.reason = "pocas_correspondencias_2d3d";
    return r;
  }

  std::vector<cv::Point3d> object;
  std::vector<cv::Point2d> normalized;
  for (std::size_t k = 0; k < c.points.size(); ++k) {
    const cv::Vec3d& X = rec.points[static_cast<std::size_t>(c.points[k])].xyz;
    object.emplace_back(X[0], X[1], X[2]);
    normalized.push_back(rec.camera.pixelToNormalized(c.observations[k].xy));
  }
  // Con coordenadas normalizadas la matriz de cámara es la identidad y el umbral se divide entre f.
  const cv::Mat I = cv::Mat::eye(3, 3, CV_64F);
  cv::Mat rvec, tvec;
  std::vector<int> inliers;
  const bool found = cv::solvePnPRansac(object, normalized, I, cv::noArray(), rvec, tvec, false, 1000,
                                        static_cast<float>(params.pnp_ransac_threshold_px / rec.camera.f),
                                        params.pnp_confidence, inliers, cv::SOLVEPNP_EPNP);
  if (!found || inliers.size() < 4) {
    r.reason = "pnp_fallido";
    return r;
  }
  std::vector<cv::Point3d> obj_in;
  std::vector<cv::Point2d> img_in;
  for (int k : inliers) {
    obj_in.push_back(object[static_cast<std::size_t>(k)]);
    img_in.push_back(normalized[static_cast<std::size_t>(k)]);
  }
  cv::solvePnPRefineLM(obj_in, img_in, I, cv::noArray(), rvec, tvec);
  cv::Mat R;
  cv::Rodrigues(rvec, R);
  const Pose pose = Pose::fromRt(cv::Matx33d(R), cv::Vec3d(tvec.at<double>(0), tvec.at<double>(1), tvec.at<double>(2)));

  // Inliers definitivos con el error en píxeles del modelo completo (incluye distorsión).
  std::vector<std::size_t> good;
  for (std::size_t k = 0; k < c.points.size(); ++k) {
    const cv::Vec3d& X = rec.points[static_cast<std::size_t>(c.points[k])].xyz;
    if (pose.toCamera(X)[2] <= 0) continue;
    if (reprojectionError(rec.camera, pose, X, c.observations[k].xy) <= params.pnp_ransac_threshold_px) good.push_back(k);
  }
  r.n_inliers = static_cast<int>(good.size());
  if (r.n_inliers < params.pnp_min_inliers) {
    r.reason = "pnp_pocos_inliers";
    return r;
  }

  Camera& cam = rec.cameras[image];
  cam.index = image;
  cam.registered = true;
  cam.reason.clear();
  cam.pose = pose;
  rec.registration_order.push_back(image);
  for (std::size_t k : good) rec.points[static_cast<std::size_t>(c.points[k])].observations.push_back(c.observations[k]);
  r.ok = true;
  return r;
}

int triangulateTracks(Reconstruction& rec, const ReconstructionParams& params, int only_with_image) {
  int added = 0;
  for (std::size_t t = 0; t < rec.tracks.size(); ++t) {
    if (rec.point_of_track[t] >= 0) continue;
    std::vector<Observation> obs;
    bool has_image = only_with_image < 0;
    for (const Observation& o : rec.tracks[t].observations) {
      const auto it = rec.cameras.find(o.image);
      if (it == rec.cameras.end() || !it->second.registered) continue;
      obs.push_back(o);
      has_image |= o.image == only_with_image;
    }
    if (obs.size() < 2 || !has_image) continue;
    std::vector<Pose> poses;
    std::vector<cv::Point2d> pixels;
    for (const Observation& o : obs) {
      poses.push_back(rec.cameras.at(o.image).pose);
      pixels.push_back(o.xy);
    }
    const TriangulationResult tr =
        triangulateChecked(rec.camera, poses, pixels, params.tri_min_angle_deg, params.max_reproj_error_px);
    if (!tr.ok) continue;
    Point3D p;
    p.xyz = tr.X;
    p.track = static_cast<int>(t);
    p.observations = std::move(obs);
    rec.point_of_track[t] = static_cast<int>(rec.points.size());
    rec.points.push_back(std::move(p));
    ++added;
  }
  return added;
}
