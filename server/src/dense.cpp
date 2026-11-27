#include "dense.h"

#include <fstream>
#include <string>

namespace fs = std::filesystem;

DenseResult runDensification(ColmapRunner& colmap, const fs::path& image_dir, const fs::path& sparse_dir,
                             const fs::path& dense_dir, const DenseParams& params) {
  fs::create_directories(dense_dir);
  const std::string size = std::to_string(params.max_image_size);
  DenseResult r;
  r.undistort_ms = colmap
                       .run("image_undistorter", {{"--image_path", image_dir.string()},
                                                  {"--input_path", sparse_dir.string()},
                                                  {"--output_path", dense_dir.string()},
                                                  {"--output_type", "COLMAP"},
                                                  {"--max_image_size", size}})
                       .ms;
  r.patch_match_ms = colmap
                         .run("patch_match_stereo", {{"--workspace_path", dense_dir.string()},
                                                     {"--workspace_format", "COLMAP"},
                                                     {"--PatchMatchStereo.geom_consistency", params.geom_consistency ? "1" : "0"},
                                                     {"--PatchMatchStereo.max_image_size", size}})
                         .ms;
  r.fused_ply = dense_dir / "fused.ply";
  r.fusion_ms = colmap
                    .run("stereo_fusion", {{"--workspace_path", dense_dir.string()},
                                           {"--workspace_format", "COLMAP"},
                                           {"--input_type", params.geom_consistency ? "geometric" : "photometric"},
                                           {"--output_path", r.fused_ply.string()}})
                    .ms;
  if (!fs::exists(r.fused_ply)) throw ColmapError("stereo_fusion no dejó " + r.fused_ply.string());
  r.dense_points = plyVertexCount(r.fused_ply);
  return r;
}

int plyVertexCount(const fs::path& ply) {
  std::ifstream in(ply, std::ios::binary);
  if (!in) return -1;
  std::string line;
  if (!std::getline(in, line) || line.rfind("ply", 0) != 0) return -1;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "end_header") break;
    if (line.rfind("element vertex ", 0) == 0) return std::stoi(line.substr(15));
  }
  return -1;
}
