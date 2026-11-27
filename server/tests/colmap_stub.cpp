#define _CRT_SECURE_NO_WARNINGS  // getenv en un ejecutable de prueba
// Ejecutable falso de COLMAP para las pruebas (no depende de COLMAP ni de la GPU).
//  - Agrega sus argumentos (uno por línea, y "---" al final) al archivo de la variable
//    COLMAP_STUB_LOG.
//  - "-h": imprime COLMAP_STUB_HELP o una cabecera de COLMAP con CUDA.
//  - Sale con el código de COLMAP_STUB_EXIT (0 por defecto) e imprime COLMAP_STUB_OUTPUT.
//  - Para stereo_fusion y poisson_mesher crea un PLY mínimo en --output_path, y para
//    image_undistorter la carpeta de salida, como haría COLMAP.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

std::string optionValue(int argc, char** argv, const std::string& option) {
  for (int k = 1; k + 1 < argc; ++k)
    if (option == argv[k]) return argv[k + 1];
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  const std::string log = env("COLMAP_STUB_LOG");
  if (!log.empty()) {
    std::ofstream out(log, std::ios::app);
    for (int k = 1; k < argc; ++k) out << argv[k] << "\n";
    out << "---\n";
  }
  const std::string command = argc > 1 ? argv[1] : "";
  if (command == "-h" || command == "help") {
    const std::string help = env("COLMAP_STUB_HELP");
    std::cout << (help.empty() ? "COLMAP 9.9.9 (Commit stub on 2026-01-01 with CUDA)\nUsage:\n" : help + "\n");
    return 0;
  }
  std::cout << env("COLMAP_STUB_OUTPUT") << "\n";
  const std::string code = env("COLMAP_STUB_EXIT");
  if (!code.empty() && code != "0") return std::atoi(code.c_str());

  if (command == "stereo_fusion" || command == "poisson_mesher") {
    const std::string path = optionValue(argc, argv, "--output_path");
    if (!path.empty()) {
      std::ofstream ply(path, std::ios::binary);
      ply << "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
             "element face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
    }
  } else if (command == "image_undistorter") {
    const std::string path = optionValue(argc, argv, "--output_path");
    if (!path.empty()) fs::create_directories(path);
  } else if (command == "mapper") {
    const std::string path = optionValue(argc, argv, "--output_path");
    if (!path.empty()) fs::create_directories(fs::path(path) / "0");
  } else if (command == "model_converter") {
    // Modelo de texto mínimo: una cámara y dos imágenes.
    const fs::path out = optionValue(argc, argv, "--output_path");
    if (!out.empty()) {
      fs::create_directories(out);
      std::ofstream(out / "cameras.txt") << "1 RADIAL 4000 3000 3093 2000.5 1500.5 0.01 0\n";
      std::ofstream(out / "images.txt") << "1 1 0 0 0 0 0 0 1 img_001.jpg\n\n2 1 0 0 0 1 0 0 1 img_002.jpg\n\n";
      std::ofstream(out / "points3D.txt") << "";
    }
  }
  return 0;
}
