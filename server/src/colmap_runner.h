// Ejecución de COLMAP como programa externo (Sprint 4): línea de comandos, salida,
// código de retorno, tiempo y detección de CUDA.
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class ColmapError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct ProcessResult {
  int exit_code = -1;
  std::string output;  // stdout y stderr juntos
  double ms = 0;
};

// Comillas al estilo de Windows cuando el argumento tiene espacios o comillas.
std::string quoteArgument(const std::string& arg);
std::string commandLine(const std::filesystem::path& exe, const std::vector<std::string>& args);

// Ejecuta y espera. Lanza ColmapError si el ejecutable no existe.
ProcessResult runProcess(const std::filesystem::path& exe, const std::vector<std::string>& args);

// A partir de la salida de "colmap -h": "... with CUDA ..." frente a "without CUDA".
bool colmapHasCuda(const std::string& help_output);
// Primera línea con "COLMAP x.y" (vacío si no aparece).
std::string colmapVersion(const std::string& help_output);

// La ruta configurada si existe; si no, colmap en el PATH; si no, C:\tools\colmap
// (COLMAP.bat o bin\colmap.exe, la ubicación que revisa scripts\check-env.ps1).
// Lanza ColmapError si no se encuentra.
std::filesystem::path findColmap(const std::string& configured);

using ColmapOptions = std::vector<std::pair<std::string, std::string>>;  // {"--opcion", "valor"}

class ColmapRunner {
 public:
  // log_file: se le agregan la línea de comandos y la salida de cada ejecución.
  ColmapRunner(std::filesystem::path exe, std::filesystem::path log_file);

  // Ejecuta "colmap <command> <opciones>". Lanza ColmapError con el final de la salida si
  // el código de retorno no es 0.
  ProcessResult run(const std::string& command, const ColmapOptions& options);
  // "colmap -h": versión y CUDA.
  ProcessResult help();

  const std::filesystem::path& exe() const { return exe_; }

 private:
  std::filesystem::path exe_;
  std::filesystem::path log_;
};
