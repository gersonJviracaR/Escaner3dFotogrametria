#include "session_pipeline.h"

#include "atomic_file.h"
#include "manifest.h"
#include "reconstruction_io.h"

#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

SessionPaths sessionPaths(const fs::path& session, const std::string& poses) {
  SessionPaths p;
  p.session = session;
  if (poses == "ours") {
    p.sparse_model = session / "colmap_ours" / "sparse" / "0";
    p.dense = session / "dense";
    p.model = session / "model";
  } else if (poses == "colmap") {
    p.sparse_model = session / "colmap_ref" / "sparse_txt";
    p.dense = session / "dense_colmap";
    p.model = session / "model_colmap";
  } else {
    throw std::invalid_argument("--poses debe ser ours o colmap (se recibió " + poses + ")");
  }
  return p;
}

std::map<int, std::string> photoNames(const fs::path& session) {
  std::map<int, std::string> names;
  for (const PhotoRecord& p : readManifest(session / "manifest.json").photos) names[p.metadata.index] = p.file;
  return names;
}

int exportOursToColmap(const fs::path& session, const fs::path& out_dir) {
  const fs::path file = session / "reconstruction" / "reconstruction.json";
  if (!fs::exists(file)) throw std::runtime_error("falta " + file.string() + ": ejecuta antes scanner_reconstruct");
  const ReconstructionFile rf = readReconstruction(file);
  const ColmapModel model = toColmapModel(rf.rec, photoNames(session));
  writeColmapText(out_dir, model);
  return static_cast<int>(model.images.size());
}

Reconstruction loadPoses(const fs::path& session, const std::string& poses) {
  if (poses == "ours") {
    const fs::path file = session / "reconstruction" / "reconstruction.json";
    if (!fs::exists(file)) throw std::runtime_error("falta " + file.string() + ": ejecuta antes scanner_reconstruct");
    return readReconstruction(file).rec;
  }
  const fs::path dir = sessionPaths(session, poses).sparse_model;
  if (!fs::exists(dir / "images.txt"))
    throw std::runtime_error("falta la referencia de COLMAP en " + dir.string() + ": ejecuta antes scanner_accuracy");
  std::map<std::string, int> index_of;
  for (const auto& [index, name] : photoNames(session)) index_of[name] = index;
  return toReconstruction(readColmapText(dir), index_of);
}

json readModelReport(const fs::path& model_dir) {
  const fs::path file = model_dir / "model_report.json";
  if (!fs::exists(file)) return json::object();
  return json::parse(readFileBytes(file));
}

void updateModelReport(const fs::path& model_dir, const std::string& section, const json& value) {
  fs::create_directories(model_dir);
  json report = readModelReport(model_dir);
  report[section] = value;
  writeFileAtomic(model_dir / "model_report.json", report.dump(2) + '\n');
}
