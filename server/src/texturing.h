// Texturizado propio (HU-21): mejor cámara por triángulo con oclusión, cartas y atlas.
//
// Convenciones: las imágenes se usan sin distorsión (pinhole con la misma f, cx, cy);
// UV por vértice de cara en [0, 1] con v hacia arriba (convención OBJ: v = 1 − y/alto).
#pragma once

#include "camera_model.h"
#include "meshing.h"
#include "pipeline_params.h"

#include <opencv2/core.hpp>

#include <array>
#include <functional>
#include <map>
#include <vector>

// Imagen sin distorsión (mismo tamaño, misma f, cx, cy, k1 = k2 = 0).
cv::Mat undistortImage(const cv::Mat& image, const CameraModel& camera);

// Mapa de profundidad (z de cámara, CV_32F, +inf donde no hay malla) de tamaño
// width x height para la cámara pinhole `camera` (sin distorsión) escalada a ese tamaño.
cv::Mat renderDepth(const Mesh& mesh, const CameraModel& camera, const Pose& pose, int width, int height);

// Normal de cada cara (por el orden de sus vértices; si la malla trae normales por vértice
// y apuntan al revés, se invierte).
std::vector<cv::Vec3f> faceNormals(const Mesh& mesh);

// Mejor cámara de cada cara (-1 si ninguna sirve): score = cos(θ) entre la normal y la
// dirección a la cámara, solo si los 3 vértices caen dentro de la imagen, delante de la
// cámara, sin oclusión (según los mapas de profundidad) y con cos(θ) >= min_cos.
std::vector<int> chooseCameras(const Mesh& mesh, const CameraModel& camera, const std::map<int, Pose>& poses,
                               const std::map<int, cv::Mat>& depth_maps, const TextureParams& params);

// Empaquetado por estantes: posición de cada rectángulo (ordenados por alto) dentro de
// un ancho max_size; devuelve vacío si no caben en max_size de alto.
std::vector<cv::Point> packShelves(const std::vector<cv::Size>& sizes, int max_size, int& used_height);

struct TextureStats {
  int faces = 0;
  int textured_faces = 0;
  double textured_pct = 0;
  std::map<int, int> faces_per_camera;
  int charts = 0;
  int atlas_width = 0;
  int atlas_height = 0;
  double chart_scale = 1;  // < 1 si las cartas no cabían y se redujeron de forma uniforme
};

struct TextureResult {
  std::vector<int> face_camera;                    // -1: sin textura (gris neutro)
  std::vector<std::array<cv::Vec2f, 3>> face_uv;   // UV de los 3 vértices de cada cara
  cv::Mat atlas;                                   // BGR
  TextureStats stats;
};

// Texturiza la malla. `load_image(index)` devuelve la foto original (con distorsión) de
// esa cámara; se llama una vez por cámara elegida, de a una (ahorra memoria).
TextureResult textureMesh(const Mesh& mesh, const CameraModel& camera, const std::map<int, Pose>& poses,
                          const std::function<cv::Mat(int)>& load_image, const TextureParams& params);
