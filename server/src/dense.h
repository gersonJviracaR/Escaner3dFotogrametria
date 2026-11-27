// Densificación con COLMAP a partir de un modelo disperso (HU-19):
// image_undistorter -> patch_match_stereo -> stereo_fusion.
#pragma once

#include "colmap_runner.h"
#include "pipeline_params.h"

#include <filesystem>
#include <string>

struct DenseResult {
  std::filesystem::path fused_ply;  // <dense_dir>/fused.ply
  int dense_points = 0;
  double undistort_ms = 0;
  double patch_match_ms = 0;
  double fusion_ms = 0;
};

// image_dir: carpeta con las fotos; sparse_dir: modelo COLMAP (texto) con las cámaras a
// densificar. Lanza ColmapError si algún paso falla o no deja fused.ply.
DenseResult runDensification(ColmapRunner& colmap, const std::filesystem::path& image_dir,
                             const std::filesystem::path& sparse_dir, const std::filesystem::path& dense_dir,
                             const DenseParams& params);

// Número de vértices declarado en la cabecera de un PLY ("element vertex N"); -1 si no hay.
int plyVertexCount(const std::filesystem::path& ply);
