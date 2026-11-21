// Salidas de la reconstrucción (sección 3.5 del Sprint 3): reconstruction.json y
// sparse_cloud.ply, y las comprobaciones informativas del reporte (IMU y anillo).
// Formatos en docs/formatos.md.
#pragma once

#include "camera_model.h"
#include "reconstruction_params.h"
#include "sfm_types.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <string>

constexpr int kReconstructionSchemaVersion = 1;

struct ReconstructionFile {
  std::string session_id;
  std::string generated_at;
  ReconstructionParams params;
  Reconstruction rec;  // al leer: sin pistas (point.track = -1)
};

nlohmann::json toJson(const ReconstructionFile& f);
// Lanza std::invalid_argument si faltan campos obligatorios.
ReconstructionFile reconstructionFromJson(const nlohmann::json& j);
void writeReconstruction(const std::filesystem::path& file, const ReconstructionFile& f);
ReconstructionFile readReconstruction(const std::filesystem::path& file);

// PLY ASCII: los puntos con su color y, al final, los centros de las cámaras
// registradas en magenta (255, 0, 255).
std::string toPly(const Reconstruction& rec);
void writePly(const std::filesystem::path& file, const Reconstruction& rec);

// Comprobación con la IMU (solo informativa): para cada par de cámaras registradas
// consecutivas por índice, el ángulo de la rotación relativa reconstruida frente a
// 2·acos|q_i·q_j| del manifiesto (la magnitud no depende de los ejes del dispositivo).
nlohmann::json imuConsistencyJson(const Reconstruction& rec, const std::map<int, Quat>& imu);

// Distancia de los centros de las cámaras registradas a su centroide.
nlohmann::json ringGeometryJson(const Reconstruction& rec);
