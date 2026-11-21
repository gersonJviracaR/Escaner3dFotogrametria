#include "http_server.h"

#include <httplib.h>

#include <iostream>

using nlohmann::json;

namespace {

constexpr std::size_t kMetadataAllowanceBytes = 1024 * 1024;  // margen para metadatos y cabeceras multipart

void sendJson(httplib::Response& res, int status, const json& body) {
  res.status = status;
  res.set_content(body.dump(), "application/json; charset=utf-8");
}

void sendError(httplib::Response& res, int status, const std::string& message) {
  sendJson(res, status, json{{"error", message}});
}

std::string defaultErrorMessage(int status) {
  switch (status) {
    case 404: return "recurso no encontrado";
    case 405: return "método no permitido";
    case 413: return "la petición excede el tamaño máximo permitido";
    default: return "error en la petición";
  }
}

}  // namespace

ScannerHttpServer::ScannerHttpServer(SessionStore& store, std::size_t maxPhotoBytes)
    : store_(store), maxPhotoBytes_(maxPhotoBytes), server_(std::make_unique<httplib::Server>()) {
  server_->set_payload_max_length(maxPhotoBytes_ + kMetadataAllowanceBytes);
  server_->set_read_timeout(std::chrono::seconds(60));
  registerRoutes();
}

ScannerHttpServer::~ScannerHttpServer() = default;

void ScannerHttpServer::registerRoutes() {
  // Toda respuesta de error sin cuerpo (404 de ruta, 413 de la librería...) sale como JSON.
  server_->set_error_handler([](const httplib::Request&, httplib::Response& res) {
    if (res.body.empty()) sendError(res, res.status, defaultErrorMessage(res.status));
    return httplib::Server::HandlerResponse::Handled;
  });

  server_->set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
    std::string what = "error interno";
    try {
      if (ep) std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      what = e.what();
    } catch (...) {
    }
    std::cerr << "[error] " << req.method << " " << req.path << ": " << what << "\n";
    sendError(res, 500, what);
  });

  server_->Get("/health", [](const httplib::Request&, httplib::Response& res) {
    sendJson(res, 200, json{{"status", "ok"}, {"version", kServerVersion}});
  });

  server_->Post("/sessions", [this](const httplib::Request& req, httplib::Response& res) {
    json body = json::object();
    if (!req.body.empty()) {
      body = json::parse(req.body, nullptr, false);
      if (body.is_discarded() || !body.is_object()) return sendError(res, 400, "el cuerpo debe ser un objeto JSON");
    }
    const json name = body.value("name", json(""));
    if (!name.is_string()) return sendError(res, 400, "'name' debe ser texto");
    const std::string id = store_.createSession(name.get<std::string>(), body.value("device", json::object()));
    std::cout << "[sesion] creada " << id << std::endl;
    sendJson(res, 201, json{{"session_id", id}});
  });

  server_->Post("/sessions/:id/photos", [this](const httplib::Request& req, httplib::Response& res) {
    const std::string id = req.path_params.at("id");
    if (!isValidSessionId(id) || !store_.exists(id)) return sendError(res, 404, "la sesión '" + id + "' no existe");
    if (!req.form.has_file("image")) return sendError(res, 400, "falta el campo 'image' (JPEG)");

    const httplib::FormData image = req.form.get_file("image");
    if (image.content.size() > maxPhotoBytes_) return sendError(res, 413, "la foto excede el tamaño máximo permitido");

    // 'metadata' puede llegar como campo de texto o como parte con content-type JSON.
    std::string metaText;
    if (req.form.has_field("metadata"))
      metaText = req.form.get_field("metadata");
    else if (req.form.has_file("metadata"))
      metaText = req.form.get_file("metadata").content;
    else
      return sendError(res, 400, "falta el campo 'metadata' (JSON)");

    const json metaJson = json::parse(metaText, nullptr, false);
    if (metaJson.is_discarded()) return sendError(res, 400, "'metadata' no es JSON válido");

    try {
      const PhotoRecord record = store_.addPhoto(id, parsePhotoMetadata(metaJson), image.content);
      std::cout << "[foto] " << id << " #" << record.metadata.index << " nitidez=" << record.quality.sharpness
                << " saturacion=" << record.quality.overexposed_ratio
                << (record.quality.accepted ? " ACEPTADA" : " RECHAZADA") << std::endl;
      sendJson(res, 200, validationJson(record));
    } catch (const MetadataError& e) {
      sendError(res, 400, e.what());
    } catch (const InvalidImageError& e) {
      sendError(res, 400, e.what());
    } catch (const SessionNotFoundError& e) {
      sendError(res, 404, e.what());
    }
  });

  server_->Post("/sessions/:id/complete", [this](const httplib::Request& req, httplib::Response& res) {
    const std::string id = req.path_params.at("id");
    if (!isValidSessionId(id)) return sendError(res, 404, "la sesión '" + id + "' no existe");
    try {
      const Summary s = store_.complete(id);
      std::cout << "[sesion] completada " << id << ": " << s.accepted << "/" << s.total << " aceptadas" << std::endl;
      sendJson(res, 200, toJson(s));
    } catch (const SessionNotFoundError& e) {
      sendError(res, 404, e.what());
    }
  });

  server_->Get("/sessions/:id/validation", [this](const httplib::Request& req, httplib::Response& res) {
    const std::string id = req.path_params.at("id");
    if (!isValidSessionId(id)) return sendError(res, 404, "la sesión '" + id + "' no existe");
    try {
      const Manifest m = store_.manifest(id);
      json photos = json::array();
      for (const PhotoRecord& p : m.photos) photos.push_back(validationJson(p));
      sendJson(res, 200, json{{"session_id", id}, {"photos", photos}, {"summary", toJson(summarize(m.photos))}});
    } catch (const SessionNotFoundError& e) {
      sendError(res, 404, e.what());
    }
  });
}

bool ScannerHttpServer::listen(const std::string& host, int port) { return server_->listen(host, port); }

int ScannerHttpServer::bindToAnyPort(const std::string& host) { return server_->bind_to_any_port(host); }

bool ScannerHttpServer::listenAfterBind() { return server_->listen_after_bind(); }

void ScannerHttpServer::waitUntilReady() const { server_->wait_until_ready(); }

void ScannerHttpServer::stop() { server_->stop(); }
