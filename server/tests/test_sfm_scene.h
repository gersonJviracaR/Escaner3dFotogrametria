// Escena sintética multivista para HU-15 a HU-18: anillo de cámaras alrededor de una
// nube de puntos, con pares vecinos, E estimada con el código del Sprint 2 y pistas.
#pragma once

#include "epipolar.h"
#include "reconstruction.h"
#include "test_scene.h"
#include "tracks.h"

#include <map>
#include <utility>
#include <vector>

namespace scene {

struct SfmScene {
  CameraModel camera;                 // intrínsecos verdaderos
  std::map<int, Pose> poses;          // por índice de foto
  std::vector<cv::Vec3d> points;      // punto k = índice k en todas las fotos
  std::map<int, std::map<int, cv::Point2d>> observed;  // foto -> (punto -> píxel)
  SfmInput input;
};

struct RingOptions {
  int n_cameras = 12;
  double step_deg = 30.0;
  int n_points = 1500;
  double noise_px = 0.3;
  std::uint64_t seed = 99;
  bool weak_camera = false;       // foto 13: solo 6 correspondencias con la foto 1
  bool separate_component = false;  // fotos 20 y 21: solo emparejadas entre sí
  double k1 = 0;                    // distorsión verdadera de la cámara
  double k2 = 0;
};

inline void addPair(SfmScene& s, std::vector<PairMatches>& matches, int i, int j) {
  PairMatches pm{i, j, {}};
  PairCandidate pc;
  pc.i = i;
  pc.j = j;
  std::vector<cv::Point2f> fi, fj;
  for (const auto& [k, pi] : s.observed[i]) {
    const auto it = s.observed[j].find(k);
    if (it == s.observed[j].end()) continue;
    InlierCorrespondence c;
    c.index_i = k;
    c.index_j = k;
    c.point_i = cv::Point2f(pi);
    c.point_j = cv::Point2f(it->second);
    pm.inliers.push_back(c);
    pc.pts_i.push_back(pi);
    pc.pts_j.push_back(it->second);
    fi.push_back(c.point_i);
    fj.push_back(c.point_j);
  }
  if (fi.size() >= 8) {
    const cv::Matx33d K(s.camera.f, 0, s.camera.cx, 0, s.camera.f, s.camera.cy, 0, 0, 1);
    MatchingParams mp;
    mp.ransac_threshold_px = 1.5;
    pc.E = estimatePairGeometry(fi, fj, K, K, mp).E;
  }
  matches.push_back(std::move(pm));
  s.input.pairs.push_back(std::move(pc));
}

inline SfmScene makeRingScene(const RingOptions& o = {}) {
  SfmScene s;
  s.camera = camera();
  s.camera.k1 = o.k1;
  s.camera.k2 = o.k2;
  s.input.camera = s.camera;
  s.input.camera.k1 = 0;  // la reconstrucción empieza sin distorsión, como con la K del EXIF
  s.input.camera.k2 = 0;
  const std::vector<Pose> ring_poses = ring(o.n_cameras, 6.0, o.step_deg);
  for (int k = 0; k < o.n_cameras; ++k) s.poses[k + 1] = ring_poses[static_cast<std::size_t>(k)];
  s.points = randomPoints(o.n_points, o.seed, 1.5);
  cv::RNG rng(o.seed + 1);

  auto observeAll = [&](int image, int from, int to) {
    for (int k = from; k < to; ++k) {
      const Pose& p = s.poses[image];
      if (p.toCamera(s.points[static_cast<std::size_t>(k)])[2] < 1.0) continue;
      const cv::Point2d u = observe(s.camera, p, s.points[static_cast<std::size_t>(k)], rng, o.noise_px);
      if (insideImage(s.camera, u)) s.observed[image][k] = u;
    }
  };
  for (int c = 1; c <= o.n_cameras; ++c) observeAll(c, 0, o.n_points);

  if (o.weak_camera) {
    s.poses[13] = lookAt({0, 0, 7.0}, {0.01, 0.01, 0});  // desde arriba
    for (int k = 0; k < 6; ++k)
      s.observed[13][k] = observe(s.camera, s.poses[13], s.points[static_cast<std::size_t>(k)], rng, o.noise_px);
  }
  if (o.separate_component) {
    // Otra nube, lejos, vista solo por las fotos 20 y 21.
    const std::size_t base = s.points.size();
    for (const cv::Vec3d& X : randomPoints(300, o.seed + 5, 1.0)) s.points.push_back(X + cv::Vec3d(50, 0, 0));
    s.poses[20] = lookAt({50, -6, 0.5}, {50, 0, 0});
    s.poses[21] = lookAt({53, -5.2, 0.5}, {50, 0, 0});
    for (int img : {20, 21}) observeAll(img, static_cast<int>(base), static_cast<int>(s.points.size()));
  }

  std::vector<PairMatches> matches;
  for (int c = 1; c <= o.n_cameras; ++c)
    for (int d : {1, 2}) {
      const int other = (c - 1 + d) % o.n_cameras + 1;
      addPair(s, matches, std::min(c, other), std::max(c, other));
    }
  if (o.weak_camera) addPair(s, matches, 1, 13);
  if (o.separate_component) addPair(s, matches, 20, 21);

  for (const auto& [image, pose] : s.poses) s.input.images.push_back(image);
  s.input.tracks = buildTracks(matches);
  return s;
}

// Similitud (Umeyama) que lleva los puntos `from` sobre `to`: to ≈ s·R·from + t.
struct Similarity {
  double s = 1;
  cv::Matx33d R = cv::Matx33d::eye();
  cv::Vec3d t{0, 0, 0};
  cv::Vec3d apply(const cv::Vec3d& x) const { return s * (R * x) + t; }
};

inline Similarity umeyama(const std::vector<cv::Vec3d>& from, const std::vector<cv::Vec3d>& to) {
  const double n = static_cast<double>(from.size());
  cv::Vec3d mf(0, 0, 0), mt(0, 0, 0);
  for (std::size_t k = 0; k < from.size(); ++k) {
    mf += from[k];
    mt += to[k];
  }
  mf *= 1.0 / n;
  mt *= 1.0 / n;
  cv::Matx33d cov = cv::Matx33d::zeros();
  double var = 0;
  for (std::size_t k = 0; k < from.size(); ++k) {
    const cv::Vec3d a = from[k] - mf, b = to[k] - mt;
    cov += cv::Matx33d(b[0] * a[0], b[0] * a[1], b[0] * a[2], b[1] * a[0], b[1] * a[1], b[1] * a[2], b[2] * a[0],
                       b[2] * a[1], b[2] * a[2]) *
           (1.0 / n);
    var += a.dot(a) / n;
  }
  cv::Matx33d U, Vt;
  cv::Matx31d D;
  cv::SVD::compute(cov, D, U, Vt);
  cv::Matx33d S = cv::Matx33d::eye();
  if (cv::determinant(U * Vt) < 0) S(2, 2) = -1;
  Similarity sim;
  sim.R = U * S * Vt;
  sim.s = (D(0) * S(0, 0) + D(1) * S(1, 1) + D(2) * S(2, 2)) / var;
  sim.t = mt - sim.s * (sim.R * mf);
  return sim;
}

inline double rotationAngleDeg(const cv::Matx33d& R) {
  const double c = std::max(-1.0, std::min(1.0, (cv::trace(R) - 1.0) / 2.0));
  return std::acos(c) * 180.0 / kPi;
}

}  // namespace scene
