#include "atomic_file.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

void writeFileAtomic(const std::filesystem::path& file, const std::string& bytes) {
  std::filesystem::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("no se pudo escribir " + tmp.string());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("error al escribir " + tmp.string());
  }
  // rename reemplaza el destino si existe (MoveFileEx con REPLACE_EXISTING en MSVC).
  std::filesystem::rename(tmp, file);
}

std::string readFileBytes(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("no se pudo leer " + file.string());
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
