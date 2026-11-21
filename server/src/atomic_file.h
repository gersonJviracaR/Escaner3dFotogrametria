// Escritura segura de archivos: temporal + renombrado (nunca deja archivos a medias).
#pragma once

#include <filesystem>
#include <string>

void writeFileAtomic(const std::filesystem::path& file, const std::string& bytes);
std::string readFileBytes(const std::filesystem::path& file);
