#include "colmap_runner.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::string quoteArgument(const std::string& arg) {
  if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) return arg;
  std::string out = "\"";
  for (char c : arg) {
    if (c == '"') out += '\\';
    out += c;
  }
  return out + "\"";
}

std::string commandLine(const fs::path& exe, const std::vector<std::string>& args) {
  std::string line = quoteArgument(exe.generic_string());
  for (const std::string& a : args) line += " " + quoteArgument(a);
  return line;
}

ProcessResult runProcess(const fs::path& exe, const std::vector<std::string>& args) {
  if (!fs::exists(exe)) throw ColmapError("no existe el ejecutable " + exe.string());
  const auto t0 = std::chrono::steady_clock::now();
  // cmd /c quita el primer y el último par de comillas: se envuelve la línea entera.
  const std::string cmd = "\"" + commandLine(exe, args) + " 2>&1\"";
#ifdef _WIN32
  FILE* pipe = _popen(cmd.c_str(), "r");
#else
  FILE* pipe = popen(cmd.c_str(), "r");
#endif
  if (!pipe) throw ColmapError("no se pudo ejecutar " + exe.string());
  ProcessResult r;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe)) r.output += buffer;
#ifdef _WIN32
  r.exit_code = _pclose(pipe);
#else
  const int status = pclose(pipe);
  r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
  r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return r;
}

std::string colmapVersion(const std::string& help_output) {
  std::istringstream in(help_output);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("COLMAP ", 0) == 0) return line;
  }
  return {};
}

bool colmapHasCuda(const std::string& help_output) {
  const std::string header = colmapVersion(help_output);
  return header.find("with CUDA") != std::string::npos && header.find("without CUDA") == std::string::npos;
}

fs::path findColmap(const std::string& configured) {
  if (!configured.empty()) {
    if (fs::exists(configured)) return configured;
    throw ColmapError("no existe COLMAP en la ruta configurada: " + configured);
  }
#ifdef _WIN32
  for (const wchar_t* name : {L"colmap.exe", L"COLMAP.bat"}) {
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, name, nullptr, MAX_PATH, found, nullptr) > 0) return fs::path(found);
  }
#endif
  for (const char* candidate : {"C:/tools/colmap/COLMAP.bat", "C:/tools/colmap/bin/colmap.exe"})
    if (fs::exists(candidate)) return candidate;
  throw ColmapError("no se encontró COLMAP (ni en el PATH ni en C:\\tools\\colmap); usa --colmap <ruta>");
}

ColmapRunner::ColmapRunner(fs::path exe, fs::path log_file) : exe_(std::move(exe)), log_(std::move(log_file)) {}

ProcessResult ColmapRunner::run(const std::string& command, const ColmapOptions& options) {
  std::vector<std::string> args = {command};
  for (const auto& [option, value] : options) {
    args.push_back(option);
    args.push_back(value);
  }
  const ProcessResult r = runProcess(exe_, args);
  if (!log_.empty()) {
    std::ofstream log(log_, std::ios::app);
    log << "> " << commandLine(exe_, args) << "\n" << r.output << "\n(código " << r.exit_code << ", "
        << static_cast<long long>(r.ms) << " ms)\n\n";
  }
  if (r.exit_code != 0) {
    const std::string tail = r.output.size() > 1500 ? r.output.substr(r.output.size() - 1500) : r.output;
    throw ColmapError("COLMAP " + command + " terminó con código " + std::to_string(r.exit_code) + ":\n" + tail);
  }
  return r;
}

ProcessResult ColmapRunner::help() { return runProcess(exe_, {"-h"}); }
