// Exportación OBJ + MTL + textura con escala opcional (HU-22).
#pragma once

#include "meshing.h"
#include "texturing.h"

#include <opencv2/core.hpp>

#include <filesystem>
#include <string>

struct ScaleSpec {
  enum class Kind { None, Reference, Factor };
  Kind kind = Kind::None;
  cv::Vec3d p1{0, 0, 0};
  cv::Vec3d p2{0, 0, 0};
  double distance_mm = 0;
  double factor = 1;
};

// "x1,y1,z1;x2,y2,z2;D_mm" (dos puntos del modelo y su distancia real en mm).
// Lanza std::invalid_argument si el formato es inválido, D <= 0 o los puntos coinciden.
ScaleSpec parseScaleRef(const std::string& text);
// Factor directo (> 0).
ScaleSpec scaleByFactor(double factor);
// D / |p1 − p2|, el factor directo o 1.
double scaleFactor(const ScaleSpec& spec);

// Texto del OBJ: v (escalados), vt (uno por vértice de cara), vn si la malla tiene normales,
// "f v/vt[/vn]" con índices base 1; "mtllib model.mtl" y "usemtl".
std::string objText(const Mesh& mesh, const TextureResult& texture, double scale, const std::string& comment);
// MTL con la textura por ruta relativa.
std::string mtlText(const std::string& texture_file);

struct ObjExport {
  double scale = 1;
  std::string unit;  // "mm" o "arbitraria"
  int vertices = 0;
  int faces = 0;
  bool closed = false;
  int open_edges = 0;
};

// Escribe model.obj, model.mtl y texture.png en `dir` (escritura segura).
ObjExport exportObj(const std::filesystem::path& dir, const Mesh& mesh, const TextureResult& texture,
                    const ScaleSpec& scale);
