// Pruebas de integración HTTP (HU-05, HU-06): levanta el servidor en un puerto
// libre y lo consulta con el cliente de cpp-httplib.
#include "http_server.h"
#include "test_images.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

class HttpIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = fs::temp_directory_path() /
            ("escaner3d_http_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    store_ = std::make_unique<SessionStore>(root_, QualityThresholds{});
    server_ = std::make_unique<ScannerHttpServer>(*store_, kMaxPhotoBytes);
    port_ = server_->bindToAnyPort("127.0.0.1");
    ASSERT_GT(port_, 0);
    thread_ = std::thread([this] { server_->listenAfterBind(); });
    server_->waitUntilReady();
    client_ = std::make_unique<httplib::Client>("127.0.0.1", port_);
    client_->set_read_timeout(std::chrono::seconds(10));
  }

  void TearDown() override {
    server_->stop();
    if (thread_.joinable()) thread_.join();
    client_.reset();
    server_.reset();
    store_.reset();
    fs::remove_all(root_);
  }

  std::string createSession(const std::string& name = "prueba") {
    auto res = client_->Post("/sessions", json{{"name", name}}.dump(), "application/json");
    EXPECT_TRUE(res);
    EXPECT_EQ(res->status, 201);
    return json::parse(res->body).at("session_id").get<std::string>();
  }

  httplib::Result uploadPhoto(const std::string& id, int index, const std::string& jpeg,
                              const std::string& metadata = "") {
    const std::string meta = metadata.empty()
                                 ? json{{"index", index},
                                        {"captured_at", "2026-11-10T15:31:02-04:00"},
                                        {"width", 640},
                                        {"height", 480},
                                        {"focal_mm", 5.6},
                                        {"focal_35mm", nullptr},
                                        {"orientation_q", {1.0, 0.0, 0.0, 0.0}}}
                                       .dump()
                                 : metadata;
    httplib::UploadFormDataItems items = {
        {"image", jpeg, "img.jpg", "image/jpeg"},
        {"metadata", meta, "", "application/json"},
    };
    return client_->Post("/sessions/" + id + "/photos", items);
  }

  static constexpr std::size_t kMaxPhotoBytes = 2 * 1024 * 1024;
  fs::path root_;
  std::unique_ptr<SessionStore> store_;
  std::unique_ptr<ScannerHttpServer> server_;
  std::unique_ptr<httplib::Client> client_;
  std::thread thread_;
  int port_ = 0;
};

void expectJsonError(const httplib::Result& res, int status) {
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, status);
  const json body = json::parse(res->body);
  EXPECT_TRUE(body.contains("error"));
  EXPECT_TRUE(body.at("error").is_string());
}

}  // namespace

TEST_F(HttpIntegrationTest, HealthReturnsOkAndVersion) {
  auto res = client_->Get("/health");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  const json body = json::parse(res->body);
  EXPECT_EQ(body.at("status"), "ok");
  EXPECT_EQ(body.at("version"), "0.1.0");
}

TEST_F(HttpIntegrationTest, CreateSessionWithAndWithoutBody) {
  const std::string id = createSession("estatua01");
  EXPECT_NE(id.find("-estatua01"), std::string::npos);
  EXPECT_TRUE(fs::is_directory(root_ / id));

  auto res = client_->Post("/sessions", "", "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201);
}

TEST_F(HttpIntegrationTest, CreateSessionWithInvalidJsonIs400) {
  expectJsonError(client_->Post("/sessions", "{no es json", "application/json"), 400);
}

TEST_F(HttpIntegrationTest, FullFlowUploadValidateComplete) {
  const std::string id = createSession();

  auto r1 = uploadPhoto(id, 1, testimg::sharpJpeg(640, 480));
  ASSERT_TRUE(r1);
  ASSERT_EQ(r1->status, 200) << r1->body;
  const json v1 = json::parse(r1->body);
  EXPECT_EQ(v1.at("index"), 1);
  EXPECT_EQ(v1.at("accepted"), true);
  EXPECT_TRUE(v1.at("reasons").empty());

  auto r2 = uploadPhoto(id, 2, testimg::blurryJpeg(640, 480));
  ASSERT_TRUE(r2);
  ASSERT_EQ(r2->status, 200);
  const json v2 = json::parse(r2->body);
  EXPECT_EQ(v2.at("accepted"), false);
  EXPECT_EQ(v2.at("reasons"), json::array({"borrosa"}));

  auto val = client_->Get("/sessions/" + id + "/validation");
  ASSERT_TRUE(val);
  EXPECT_EQ(val->status, 200);
  const json vj = json::parse(val->body);
  EXPECT_EQ(vj.at("photos").size(), 2u);
  EXPECT_EQ(vj.at("summary").at("rejected"), json::array({2}));

  auto done = client_->Post("/sessions/" + id + "/complete", "", "application/json");
  ASSERT_TRUE(done);
  EXPECT_EQ(done->status, 200);
  EXPECT_EQ(json::parse(done->body), json::parse(R"({"total":2,"accepted":1,"rejected":[2]})"));

  EXPECT_TRUE(fs::exists(root_ / id / "img_001.jpg"));
  EXPECT_TRUE(fs::exists(root_ / id / "img_002.jpg"));
  EXPECT_TRUE(fs::exists(root_ / id / "manifest.json"));
}

TEST_F(HttpIntegrationTest, RetakeReplacesRejectedPhoto) {
  const std::string id = createSession();
  uploadPhoto(id, 3, testimg::blurryJpeg(640, 480));
  auto r = uploadPhoto(id, 3, testimg::sharpJpeg(640, 480));
  ASSERT_TRUE(r);
  EXPECT_EQ(json::parse(r->body).at("accepted"), true);
  auto done = client_->Post("/sessions/" + id + "/complete", "", "application/json");
  ASSERT_TRUE(done);
  EXPECT_EQ(json::parse(done->body).at("total"), 1);
  EXPECT_EQ(json::parse(done->body).at("accepted"), 1);
}

TEST_F(HttpIntegrationTest, UnknownSessionIs404) {
  expectJsonError(uploadPhoto("20000101-000000-nada", 1, testimg::sharpJpeg(64, 48)), 404);
  expectJsonError(client_->Post("/sessions/20000101-000000-nada/complete", "", "application/json"), 404);
  expectJsonError(client_->Get("/sessions/20000101-000000-nada/validation"), 404);
}

TEST_F(HttpIntegrationTest, UnknownRouteIs404Json) { expectJsonError(client_->Get("/no-existe"), 404); }

TEST_F(HttpIntegrationTest, CorruptJpegIs400) {
  const std::string id = createSession();
  expectJsonError(uploadPhoto(id, 1, "esto no es un jpeg"), 400);
}

TEST_F(HttpIntegrationTest, MalformedMetadataIs400) {
  const std::string id = createSession();
  expectJsonError(uploadPhoto(id, 1, testimg::sharpJpeg(64, 48), "{roto"), 400);
  expectJsonError(uploadPhoto(id, 1, testimg::sharpJpeg(64, 48), R"({"index": 0})"), 400);
}

TEST_F(HttpIntegrationTest, MissingFieldsIs400) {
  const std::string id = createSession();
  httplib::UploadFormDataItems onlyMeta = {{"metadata", R"({"index":1})", "", "application/json"}};
  expectJsonError(client_->Post("/sessions/" + id + "/photos", onlyMeta), 400);
  httplib::UploadFormDataItems onlyImage = {{"image", testimg::sharpJpeg(64, 48), "a.jpg", "image/jpeg"}};
  expectJsonError(client_->Post("/sessions/" + id + "/photos", onlyImage), 400);
  expectJsonError(client_->Post("/sessions/" + id + "/photos", "{}", "application/json"), 400);
}

TEST_F(HttpIntegrationTest, OversizedPhotoIs413) {
  const std::string id = createSession();
  const std::string huge(kMaxPhotoBytes + 1024, 'x');
  auto res = uploadPhoto(id, 1, huge);
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 413);
}
