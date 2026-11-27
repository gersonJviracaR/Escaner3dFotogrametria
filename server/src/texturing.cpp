#include "texturing.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

cv::Matx33d intrinsicMatrix(const CameraModel& c) { return {c.f, 0, c.cx, 0, c.f, c.cy, 0, 0, 1}; }

// Proyección pinhole (sin distorsión) de un punto en coordenadas de cámara.
cv::Point2d pinhole(const CameraModel& c, const cv::Vec3d& x) {
  return {c.f * x[0] / x[2] + c.cx, c.f * x[1] / x[2] + c.cy};
}

CameraModel scaled(const CameraModel& c, int width, int height) {
  CameraModel s = c;
  const double k = static_cast<double>(width) / c.width;
  s.f *= k;
  s.cx = (c.cx + 0.5) * k - 0.5;
  s.cy = (c.cy + 0.5) * k - 0.5;
  s.width = width;
  s.height = height;
  s.k1 = s.k2 = 0;
  return s;
}

int findRoot(std::vector<int>& parent, int x) {
  while (parent[static_cast<std::size_t>(x)] != x) {
    parent[static_cast<std::size_t>(x)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(x)])];
    x = parent[static_cast<std::size_t>(x)];
  }
  return x;
}

}  // namespace

cv::Mat undistortImage(const cv::Mat& image, const CameraModel& camera) {
  if (camera.k1 == 0 && camera.k2 == 0) return image.clone();
  // Nuestro modelo radial (k1, k2) es el de OpenCV con p1 = p2 = k3 = 0.
  cv::Mat out;
  const cv::Mat K(intrinsicMatrix(camera));
  cv::undistort(image, out, K, cv::Mat(cv::Vec4d(camera.k1, camera.k2, 0, 0)), K);
  return out;
}

cv::Mat renderDepth(const Mesh& mesh, const CameraModel& camera, const Pose& pose, int width, int height) {
  const CameraModel c = scaled(camera, width, height);
  cv::Mat depth(height, width, CV_32F, cv::Scalar(kInf));
  const cv::Matx33d R = pose.R();
  std::vector<cv::Vec3d> xc(mesh.vertices.size());
  for (std::size_t k = 0; k < mesh.vertices.size(); ++k) xc[k] = R * cv::Vec3d(mesh.vertices[k]) + pose.t;
  for (const cv::Vec3i& f : mesh.faces) {
    cv::Point2d p[3];
    double invz[3];
    bool ok = true;
    for (int k = 0; k < 3; ++k) {
      const cv::Vec3d& x = xc[static_cast<std::size_t>(f[k])];
      if (x[2] <= 1e-6) ok = false;
      if (!ok) break;
      p[k] = pinhole(c, x);
      invz[k] = 1.0 / x[2];
    }
    if (!ok) continue;
    const double area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
    if (std::abs(area) < 1e-12) continue;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({p[0].x, p[1].x, p[2].x}))));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max({p[0].x, p[1].x, p[2].x}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({p[0].y, p[1].y, p[2].y}))));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(std::max({p[0].y, p[1].y, p[2].y}))));
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        const double w0 = ((p[1].x - x) * (p[2].y - y) - (p[2].x - x) * (p[1].y - y)) / area;
        const double w1 = ((p[2].x - x) * (p[0].y - y) - (p[0].x - x) * (p[2].y - y)) / area;
        const double w2 = 1.0 - w0 - w1;
        if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
        const float z = static_cast<float>(1.0 / (w0 * invz[0] + w1 * invz[1] + w2 * invz[2]));  // correcto en perspectiva
        float& d = depth.at<float>(y, x);
        if (z < d) d = z;
      }
  }
  return depth;
}

std::vector<cv::Vec3f> faceNormals(const Mesh& mesh) {
  const bool has_vertex_normals = mesh.normals.size() == mesh.vertices.size() && !mesh.normals.empty();
  std::vector<cv::Vec3f> out;
  out.reserve(mesh.faces.size());
  for (const cv::Vec3i& f : mesh.faces) {
    const cv::Vec3f& a = mesh.vertices[static_cast<std::size_t>(f[0])];
    cv::Vec3f n = (mesh.vertices[static_cast<std::size_t>(f[1])] - a).cross(mesh.vertices[static_cast<std::size_t>(f[2])] - a);
    const float len = static_cast<float>(cv::norm(n));
    if (len > 0) n *= 1.0f / len;
    if (has_vertex_normals) {
      const cv::Vec3f avg = mesh.normals[static_cast<std::size_t>(f[0])] + mesh.normals[static_cast<std::size_t>(f[1])] +
                            mesh.normals[static_cast<std::size_t>(f[2])];
      if (n.dot(avg) < 0) n = -n;
    }
    out.push_back(n);
  }
  return out;
}

std::vector<int> chooseCameras(const Mesh& mesh, const CameraModel& camera, const std::map<int, Pose>& poses,
                               const std::map<int, cv::Mat>& depth_maps, const TextureParams& params) {
  const std::vector<cv::Vec3f> normals = faceNormals(mesh);
  std::vector<int> chosen(mesh.faces.size(), -1);
  std::vector<double> best(mesh.faces.size(), -1);
  for (const auto& [index, pose] : poses) {
    const cv::Matx33d R = pose.R();
    const cv::Vec3d C = pose.center();
    const auto dm = depth_maps.find(index);
    const cv::Mat* depth = dm != depth_maps.end() ? &dm->second : nullptr;
    const CameraModel small = depth ? scaled(camera, depth->cols, depth->rows) : camera;
    for (std::size_t k = 0; k < mesh.faces.size(); ++k) {
      const cv::Vec3i& f = mesh.faces[k];
      cv::Vec3d centroid(0, 0, 0);
      bool inside = true;
      for (int q = 0; q < 3 && inside; ++q) {
        const cv::Vec3d X(mesh.vertices[static_cast<std::size_t>(f[q])]);
        centroid += X / 3.0;
        const cv::Vec3d x = R * X + pose.t;
        if (x[2] <= 0) {
          inside = false;
          break;
        }
        const cv::Point2d u = pinhole(camera, x);
        inside = u.x >= 0 && u.y >= 0 && u.x <= camera.width - 1 && u.y <= camera.height - 1;
      }
      if (!inside) continue;
      const cv::Vec3d to_cam = cv::normalize(C - centroid);
      const double cosine = to_cam.dot(cv::Vec3d(normals[k]));
      if (cosine < params.min_cos || cosine <= best[k]) continue;
      if (depth) {
        const cv::Vec3d xc = R * centroid + pose.t;
        const cv::Point2d u = pinhole(small, xc);
        const int px = static_cast<int>(std::lround(u.x)), py = static_cast<int>(std::lround(u.y));
        if (px >= 0 && py >= 0 && px < depth->cols && py < depth->rows) {
          const float d = depth->at<float>(py, px);
          // Tapado si hay otra superficie claramente más cerca (1 % de tolerancia).
          if (std::isfinite(d) && xc[2] > d * 1.01 + 1e-9) continue;
        }
      }
      best[k] = cosine;
      chosen[k] = index;
    }
  }
  return chosen;
}

std::vector<cv::Point> packShelves(const std::vector<cv::Size>& sizes, int max_size, int& used_height) {
  used_height = 0;
  std::vector<std::size_t> order(sizes.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::stable_sort(order.begin(), order.end(),
                   [&](std::size_t a, std::size_t b) { return sizes[a].height > sizes[b].height; });
  std::vector<cv::Point> pos(sizes.size());
  int x = 0, y = 0, shelf = 0;
  for (std::size_t k : order) {
    const cv::Size s = sizes[k];
    if (s.width > max_size || s.height > max_size) return {};
    if (x + s.width > max_size) {  // estante nuevo
      y += shelf;
      x = 0;
      shelf = 0;
    }
    if (y + s.height > max_size) return {};
    pos[k] = cv::Point(x, y);
    x += s.width;
    shelf = std::max(shelf, s.height);
    used_height = std::max(used_height, y + s.height);
  }
  return pos;
}

TextureResult textureMesh(const Mesh& mesh, const CameraModel& camera, const std::map<int, Pose>& poses,
                          const std::function<cv::Mat(int)>& load_image, const TextureParams& params) {
  TextureResult r;
  const std::size_t nf = mesh.faces.size();
  r.stats.faces = static_cast<int>(nf);

  // 1. Mapas de profundidad y mejor cámara por cara.
  const double ds = static_cast<double>(params.depth_map_long_side) / std::max(camera.width, camera.height);
  const int dw = std::max(1, static_cast<int>(std::lround(camera.width * std::min(1.0, ds))));
  const int dh = std::max(1, static_cast<int>(std::lround(camera.height * std::min(1.0, ds))));
  std::map<int, cv::Mat> depth;
  for (const auto& [index, pose] : poses) depth[index] = renderDepth(mesh, camera, pose, dw, dh);
  r.face_camera = chooseCameras(mesh, camera, poses, depth, params);

  // 2. Cartas: caras vecinas (comparten arista) con la misma cámara.
  std::vector<int> parent(nf);
  std::iota(parent.begin(), parent.end(), 0);
  std::map<std::pair<int, int>, int> edge_face;
  for (std::size_t k = 0; k < nf; ++k) {
    if (r.face_camera[k] < 0) continue;
    for (int q = 0; q < 3; ++q) {
      const int a = mesh.faces[k][q], b = mesh.faces[k][(q + 1) % 3];
      const auto key = std::make_pair(std::min(a, b), std::max(a, b));
      const auto it = edge_face.find(key);
      if (it == edge_face.end()) {
        edge_face[key] = static_cast<int>(k);
      } else if (r.face_camera[static_cast<std::size_t>(it->second)] == r.face_camera[k]) {
        parent[static_cast<std::size_t>(findRoot(parent, static_cast<int>(k)))] = findRoot(parent, it->second);
      }
    }
  }
  std::map<int, int> chart_of_root;
  std::vector<int> chart(nf, -1);
  std::vector<int> chart_camera;
  std::vector<cv::Rect2d> bounds;  // en píxeles de la imagen original sin distorsión
  std::vector<std::array<cv::Point2d, 3>> proj(nf);
  for (std::size_t k = 0; k < nf; ++k) {
    const int cam = r.face_camera[k];
    if (cam < 0) continue;
    const int root = findRoot(parent, static_cast<int>(k));
    const auto [it, inserted] = chart_of_root.emplace(root, static_cast<int>(chart_camera.size()));
    if (inserted) {
      chart_camera.push_back(cam);
      bounds.emplace_back(1e30, 1e30, -1e30, -1e30);  // (x0, y0, x1, y1) provisional
    }
    chart[k] = it->second;
    const Pose& pose = poses.at(cam);
    cv::Rect2d& b = bounds[static_cast<std::size_t>(it->second)];
    for (int q = 0; q < 3; ++q) {
      const cv::Point2d u = pinhole(camera, pose.toCamera(cv::Vec3d(mesh.vertices[static_cast<std::size_t>(mesh.faces[k][q])])));
      proj[k][static_cast<std::size_t>(q)] = u;
      b.x = std::min(b.x, u.x), b.y = std::min(b.y, u.y);
      b.width = std::max(b.width, u.x), b.height = std::max(b.height, u.y);
    }
  }
  r.stats.charts = static_cast<int>(chart_camera.size());
  const int pad = params.padding_px;
  std::vector<cv::Rect> rects;  // región de la imagen (con relleno) de cada carta
  double total_area = 0;
  for (const cv::Rect2d& b : bounds) {
    const int x0 = std::max(0, static_cast<int>(std::floor(b.x)) - pad);
    const int y0 = std::max(0, static_cast<int>(std::floor(b.y)) - pad);
    const int x1 = std::min(camera.width, static_cast<int>(std::ceil(b.width)) + 1 + pad);
    const int y1 = std::min(camera.height, static_cast<int>(std::ceil(b.height)) + 1 + pad);
    rects.emplace_back(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0));
    total_area += static_cast<double>(rects.back().area());
  }

  // 3. Empaquetado (con un bloque gris para las caras sin textura); si no cabe, se reduce.
  const int gray_block = 8;
  const double max_area = static_cast<double>(params.atlas_max_size) * params.atlas_max_size;
  double s = std::min(1.0, std::sqrt(0.85 * max_area / std::max(1.0, total_area)));
  std::vector<cv::Size> sizes;
  std::vector<cv::Point> pos;
  int used_height = 0;
  for (int attempt = 0; attempt < 60; ++attempt) {
    sizes.clear();
    for (const cv::Rect& rc : rects)
      sizes.emplace_back(std::max(1, static_cast<int>(std::lround(rc.width * s))),
                         std::max(1, static_cast<int>(std::lround(rc.height * s))));
    sizes.emplace_back(gray_block, gray_block);
    pos = packShelves(sizes, params.atlas_max_size, used_height);
    if (!pos.empty()) break;
    s *= 0.9;
  }
  if (pos.empty()) throw std::runtime_error("las cartas no caben en el atlas ni reduciéndolas");
  r.stats.chart_scale = s;
  int atlas_w = 0;
  for (std::size_t k = 0; k < sizes.size(); ++k) atlas_w = std::max(atlas_w, pos[k].x + sizes[k].width);
  r.atlas = cv::Mat(used_height, atlas_w, CV_8UC3, cv::Scalar(128, 128, 128));
  r.stats.atlas_width = atlas_w;
  r.stats.atlas_height = used_height;

  // 4. Copia de píxeles, de a una cámara.
  std::map<int, std::vector<int>> charts_by_camera;
  for (std::size_t c = 0; c < chart_camera.size(); ++c) charts_by_camera[chart_camera[c]].push_back(static_cast<int>(c));
  for (const auto& [cam, list] : charts_by_camera) {
    const cv::Mat und = undistortImage(load_image(cam), camera);
    for (int c : list) {
      const cv::Rect& rc = rects[static_cast<std::size_t>(c)];
      const cv::Size& sz = sizes[static_cast<std::size_t>(c)];
      cv::Mat patch;
      cv::resize(und(rc & cv::Rect(0, 0, und.cols, und.rows)), patch, sz, 0, 0,
                 s < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);
      patch.copyTo(r.atlas(cv::Rect(pos[static_cast<std::size_t>(c)], sz)));
    }
  }

  // 5. UV por vértice de cara (v hacia arriba).
  const double W = atlas_w, H = used_height;
  const cv::Point gray_pos = pos.back();
  const cv::Vec2f gray_uv(static_cast<float>((gray_pos.x + gray_block / 2.0) / W),
                          static_cast<float>(1.0 - (gray_pos.y + gray_block / 2.0) / H));
  r.face_uv.assign(nf, {gray_uv, gray_uv, gray_uv});
  for (std::size_t k = 0; k < nf; ++k) {
    if (chart[k] < 0) continue;
    const std::size_t c = static_cast<std::size_t>(chart[k]);
    const cv::Rect& rc = rects[c];
    const double sx = static_cast<double>(sizes[c].width) / rc.width;
    const double sy = static_cast<double>(sizes[c].height) / rc.height;
    for (int q = 0; q < 3; ++q) {
      const cv::Point2d& u = proj[k][static_cast<std::size_t>(q)];
      const double ax = pos[c].x + (u.x - rc.x + 0.5) * sx;
      const double ay = pos[c].y + (u.y - rc.y + 0.5) * sy;
      r.face_uv[k][static_cast<std::size_t>(q)] =
          cv::Vec2f(static_cast<float>(std::clamp(ax / W, 0.0, 1.0)), static_cast<float>(std::clamp(1.0 - ay / H, 0.0, 1.0)));
    }
    ++r.stats.textured_faces;
    ++r.stats.faces_per_camera[r.face_camera[k]];
  }
  r.stats.textured_pct = nf ? 100.0 * r.stats.textured_faces / static_cast<double>(nf) : 0.0;
  return r;
}
