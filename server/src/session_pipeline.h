// Rutas y pasos por sesión del Epic 4, compartidos por scanner_dense, scanner_texture,
// scanner_view y scanner_accuracy.
//
// Dos fuentes de poses (decisión del usuario en T0b):
//  - "ours" (por defecto, HU-19): las poses refinadas del Sprint 3.
//  - "colmap" (comparación documentada): las de la referencia de COLMAP (colmap_ref).
#pragma once

#include "colmap_io.h"
#include "sfm_types.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <string>

struct SessionPaths {
  std::filesystem::path session;
  std::filesystem::path sparse_model;  // modelo COLMAP de texto con las poses a usar
  std::filesystem::path dense;         // espacio de trabajo de COLMAP y fused.ply
  std::filesystem::path model;         // mallas, OBJ, textura y model_report.json
};

// ours: colmap_ours/sparse/0, dense, model. colmap: colmap_ref/sparse_txt, dense_colmap, model_colmap.
// Lanza std::invalid_argument si `poses` no es "ours" ni "colmap".
SessionPaths sessionPaths(const std::filesystem::path& session, const std::string& poses);

// Nombre de archivo de cada foto del manifiesto (por índice).
std::map<int, std::string> photoNames(const std::filesystem::path& session);

// Exporta reconstruction/reconstruction.json a formato COLMAP en `out_dir` (HU-19).
// Devuelve el número de cámaras exportadas. Lanza std::runtime_error si falta la reconstrucción.
int exportOursToColmap(const std::filesystem::path& session, const std::filesystem::path& out_dir);

// Cámaras e intrínsecos de la fuente de poses elegida, como Reconstruction.
Reconstruction loadPoses(const std::filesystem::path& session, const std::string& poses);

// model_report.json: cada etapa escribe su sección (escritura segura) sin borrar las demás.
void updateModelReport(const std::filesystem::path& model_dir, const std::string& section, const nlohmann::json& value);
nlohmann::json readModelReport(const std::filesystem::path& model_dir);
