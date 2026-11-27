// Visor por software (HU-23): nube y malla texturizada en una vista general y otra de
// detalle, con z-buffer. La ventana interactiva vive en tools/scanner_view.cpp.
#pragma once

#include "camera_model.h"
#include "meshing.h"

#include <opencv2/core.hpp>

#include <array>
#include <vector>

// Cámara en órbita alrededor de `target` (mundo con z hacia arriba).
// Centro = target + distance · (cos(pitch)·cos(yaw), cos(pitch)·sin(yaw), sin(pitch)).
struct ViewCamera {
  cv::Vec3d target{0, 0, 0};
  double yaw_deg = 0;
  double pitch_deg = 20;
  double distance = 5;
  double fov_deg = 45;  // vertical
};

Pose orbitPose(const ViewCamera& view);
// Píxel de X en una vista de width x height (pinhole con el fov vertical de la vista).
cv::Point2d projectView(const ViewCamera& view, int width, int height, const cv::Vec3d& X);

struct RenderModel {
  std::vector<cv::Vec3f> points;       // nube (si no hay malla o además de ella)
  std::vector<cv::Vec3b> point_colors; // RGB, uno por punto (opcional)
  Mesh mesh;                           // malla (opcional)
  std::vector<std::array<cv::Vec2f, 3>> face_uv;  // UV por cara (opcional; con texture)
  cv::Mat texture;                     // atlas BGR (opcional)
  std::vector<cv::Vec3f> camera_centers;  // se dibujan en magenta
};

// Índices de las caras a dibujar: todas si caben, si no una de cada k (decimación simple).
std::vector<int> decimateFaces(int n_faces, int max_faces);

// Imagen BGR de la vista; fondo gris oscuro.
cv::Mat renderView(const RenderModel& model, const ViewCamera& view, int width, int height, int max_faces);

// Vista que encuadra el modelo (centroide y radio).
ViewCamera fitView(const RenderModel& model);
// Vista de detalle: misma orientación que `general`, centrada en `point` y más cerca.
ViewCamera detailView(const ViewCamera& general, const cv::Vec3d& point, double zoom = 4.0);

// Las dos vistas lado a lado (cada una de width/2 x height) con su título.
cv::Mat renderTwoViews(const RenderModel& model, const ViewCamera& general, const ViewCamera& detail, int width,
                       int height, int max_faces);
