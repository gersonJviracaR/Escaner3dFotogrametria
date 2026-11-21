// Servidor HTTP con el contrato de docs/api.md (HU-05, HU-06).
#pragma once

#include "session_store.h"

#include <cstddef>
#include <memory>
#include <string>

namespace httplib {
class Server;
}

constexpr const char* kServerVersion = "0.1.0";

class ScannerHttpServer {
 public:
  ScannerHttpServer(SessionStore& store, std::size_t maxPhotoBytes);
  ~ScannerHttpServer();
  ScannerHttpServer(const ScannerHttpServer&) = delete;
  ScannerHttpServer& operator=(const ScannerHttpServer&) = delete;

  // Bloquea hasta stop(). Devuelve false si no pudo escuchar en el puerto.
  bool listen(const std::string& host, int port);
  // Para pruebas: enlaza a un puerto libre y devuelve su número (o -1).
  int bindToAnyPort(const std::string& host);
  bool listenAfterBind();
  void waitUntilReady() const;
  void stop();

 private:
  void registerRoutes();

  SessionStore& store_;
  std::size_t maxPhotoBytes_;
  std::unique_ptr<httplib::Server> server_;
};
