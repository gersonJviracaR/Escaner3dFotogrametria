#include "viewer.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
const cv::Vec3b kBackground(40, 40, 40);

double focal(const ViewCamera& v, int height) { return (height / 2.0) / std::tan(v.fov_deg * kDegToRad / 2.0); }

struct Projector {
  Pose pose;
  double f, cx, cy;
  Projector(const ViewCamera& v, int w, int h) : pose(orbitPose(v)), f(focal(v, h)), cx((w - 1) / 2.0), cy((h - 1) / 2.0) {}
  // Coordenadas de cámara y píxel.
  cv::Vec3d cam(const cv::Vec3d& X) const { return pose.toCamera(X); }
  cv::Point2d px(const cv::Vec3d& x) const { return {f * x[0] / x[2] + cx, f * x[1] / x[2] + cy}; }
};

}  // namespace

Pose orbitPose(const ViewCamera& v) {
  const double y = v.yaw_deg * kDegToRad, p = v.pitch_deg * kDegToRad;
  const cv::Vec3d C = v.target + v.distance * cv::Vec3d(std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p));
  const cv::Vec3d z = cv::normalize(v.target - C);
  cv::Vec3d up(0, 0, 1);
  if (std::abs(z.dot(up)) > 0.999) up = cv::Vec3d(0, 1, 0);
  const cv::Vec3d x = cv::normalize(z.cross(up));
  const cv::Vec3d yv = z.cross(x);
  const cv::Matx33d R(x[0], x[1], x[2], yv[0], yv[1], yv[2], z[0], z[1], z[2]);
  return Pose::fromRt(R, -(R * C));
}

cv::Point2d projectView(const ViewCamera& view, int width, int height, const cv::Vec3d& X) {
  const Projector pr(view, width, height);
  return pr.px(pr.cam(X));
}

std::vector<int> decimateFaces(int n_faces, int max_faces) {
  std::vector<int> idx;
  if (n_faces <= 0) return idx;
  const int step = n_faces <= max_faces ? 1 : (n_faces + max_faces - 1) / max_faces;
  for (int k = 0; k < n_faces; k += step) idx.push_back(k);
  return idx;
}

cv::Mat renderView(const RenderModel& model, const ViewCamera& view, int width, int height, int max_faces) {
  cv::Mat img(height, width, CV_8UC3, cv::Scalar(kBackground[0], kBackground[1], kBackground[2]));
  cv::Mat zbuf(height, width, CV_32F, cv::Scalar(std::numeric_limits<float>::infinity()));
  const Projector pr(view, width, height);
  const double near_z = 1e-3 * std::max(1e-9, view.distance);

  // Malla: triángulos con z-buffer; textura con coordenadas baricéntricas corregidas por perspectiva.
  const Mesh& m = model.mesh;
  const bool textured = !model.texture.empty() && model.face_uv.size() == m.faces.size();
  const bool colored = m.colors.size() == m.vertices.size() && !m.colors.empty();
  for (int fi : decimateFaces(static_cast<int>(m.faces.size()), max_faces)) {
    const cv::Vec3i& f = m.faces[static_cast<std::size_t>(fi)];
    cv::Vec3d xc[3];
    cv::Point2d p[3];
    bool ok = true;
    for (int k = 0; k < 3 && ok; ++k) {
      xc[k] = pr.cam(cv::Vec3d(m.vertices[static_cast<std::size_t>(f[k])]));
      ok = xc[k][2] > near_z;
      if (ok) p[k] = pr.px(xc[k]);
    }
    if (!ok) continue;
    const double area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
    if (std::abs(area) < 1e-12) continue;
    // Sombreado simple (sin textura): según el ángulo con la dirección de vista.
    const cv::Vec3d n = cv::normalize((xc[1] - xc[0]).cross(xc[2] - xc[0]));
    const double shade = 0.35 + 0.65 * std::abs(n[2]);
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({p[0].x, p[1].x, p[2].x}))));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max({p[0].x, p[1].x, p[2].x}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({p[0].y, p[1].y, p[2].y}))));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(std::max({p[0].y, p[1].y, p[2].y}))));
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        double w[3];
        w[0] = ((p[1].x - x) * (p[2].y - y) - (p[2].x - x) * (p[1].y - y)) / area;
        w[1] = ((p[2].x - x) * (p[0].y - y) - (p[0].x - x) * (p[2].y - y)) / area;
        w[2] = 1.0 - w[0] - w[1];
        if (w[0] < -1e-9 || w[1] < -1e-9 || w[2] < -1e-9) continue;
        const double invz = w[0] / xc[0][2] + w[1] / xc[1][2] + w[2] / xc[2][2];
        const float z = static_cast<float>(1.0 / invz);
        float& zb = zbuf.at<float>(y, x);
        if (z >= zb) continue;
        zb = z;
        cv::Vec3b color;
        if (textured) {
          const auto& uv = model.face_uv[static_cast<std::size_t>(fi)];
          double u = 0, v = 0;
          for (int k = 0; k < 3; ++k) {
            u += w[k] * uv[static_cast<std::size_t>(k)][0] / xc[k][2];
            v += w[k] * uv[static_cast<std::size_t>(k)][1] / xc[k][2];
          }
          u /= invz;
          v /= invz;
          const int tx = std::clamp(static_cast<int>(u * model.texture.cols), 0, model.texture.cols - 1);
          const int ty = std::clamp(static_cast<int>((1.0 - v) * model.texture.rows), 0, model.texture.rows - 1);
          color = model.texture.at<cv::Vec3b>(ty, tx);
        } else if (colored) {
          cv::Vec3d c(0, 0, 0);
          for (int k = 0; k < 3; ++k) c += w[k] * cv::Vec3d(m.colors[static_cast<std::size_t>(f[k])]);
          color = cv::Vec3b(cv::saturate_cast<uchar>(c[2] * shade), cv::saturate_cast<uchar>(c[1] * shade),
                            cv::saturate_cast<uchar>(c[0] * shade));
        } else {
          const uchar g = cv::saturate_cast<uchar>(200 * shade);
          color = cv::Vec3b(g, g, g);
        }
        img.at<cv::Vec3b>(y, x) = color;
      }
  }

  // Puntos: cuadrados de 2x2 con z-buffer.
  const bool point_colors = model.point_colors.size() == model.points.size();
  for (std::size_t k = 0; k < model.points.size(); ++k) {
    const cv::Vec3d xc = pr.cam(cv::Vec3d(model.points[k]));
    if (xc[2] <= near_z) continue;
    const cv::Point2d p = pr.px(xc);
    const int px = static_cast<int>(std::lround(p.x)), py = static_cast<int>(std::lround(p.y));
    const cv::Vec3b rgb = point_colors ? model.point_colors[k] : cv::Vec3b(230, 230, 230);
    for (int dy = 0; dy < 2; ++dy)
      for (int dx = 0; dx < 2; ++dx) {
        const int x = px + dx, y = py + dy;
        if (x < 0 || y < 0 || x >= width || y >= height) continue;
        float& zb = zbuf.at<float>(y, x);
        if (static_cast<float>(xc[2]) >= zb) continue;
        zb = static_cast<float>(xc[2]);
        img.at<cv::Vec3b>(y, x) = cv::Vec3b(rgb[2], rgb[1], rgb[0]);
      }
  }

  // Centros de cámara (magenta), encima de todo.
  for (const cv::Vec3f& c : model.camera_centers) {
    const cv::Vec3d xc = pr.cam(cv::Vec3d(c));
    if (xc[2] <= near_z) continue;
    cv::circle(img, pr.px(xc), 4, cv::Scalar(255, 0, 255), -1, cv::LINE_AA);
  }
  return img;
}

ViewCamera fitView(const RenderModel& model) {
  std::vector<cv::Vec3d> pts;
  for (const cv::Vec3f& v : model.mesh.vertices) pts.emplace_back(v);
  if (pts.empty())
    for (const cv::Vec3f& v : model.points) pts.emplace_back(v);
  ViewCamera view;
  if (pts.empty()) return view;
  cv::Vec3d c(0, 0, 0);
  for (const cv::Vec3d& p : pts) c += p;
  c *= 1.0 / static_cast<double>(pts.size());
  // Radio robusto: percentil 95 de las distancias al centroide (la nube real tiene atípicos).
  std::vector<double> d;
  d.reserve(pts.size());
  for (const cv::Vec3d& p : pts) d.push_back(cv::norm(p - c));
  std::sort(d.begin(), d.end());
  const double radius = std::max(1e-6, d[std::min(d.size() - 1, d.size() * 95 / 100)]);
  view.target = c;
  view.distance = 1.15 * radius / std::sin(view.fov_deg * kDegToRad / 2.0);
  return view;
}

ViewCamera detailView(const ViewCamera& general, const cv::Vec3d& point, double zoom) {
  ViewCamera d = general;
  d.target = point;
  d.distance = general.distance / zoom;
  return d;
}

cv::Mat renderTwoViews(const RenderModel& model, const ViewCamera& general, const ViewCamera& detail, int width,
                       int height, int max_faces) {
  const int half = width / 2;
  cv::Mat out(height, width, CV_8UC3, cv::Scalar(kBackground[0], kBackground[1], kBackground[2]));
  const cv::Mat a = renderView(model, general, half, height, max_faces);
  const cv::Mat b = renderView(model, detail, width - half, height, max_faces);
  a.copyTo(out(cv::Rect(0, 0, half, height)));
  b.copyTo(out(cv::Rect(half, 0, width - half, height)));
  cv::line(out, {half, 0}, {half, height - 1}, cv::Scalar(90, 90, 90), 1);
  cv::putText(out, "General", {10, 22}, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
  cv::putText(out, "Detalle", {half + 10, 22}, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
  return out;
}
