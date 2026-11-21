// Pruebas de HU-05: almacenamiento de sesiones en disco.
#include "session_store.h"
#include "test_images.h"

#include <gtest/gtest.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

class SessionStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = fs::temp_directory_path() /
            ("escaner3d_store_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root_);
  }
  void TearDown() override { fs::remove_all(root_); }

  static PhotoMetadata meta(int index) {
    PhotoMetadata m;
    m.index = index;
    m.captured_at = "2026-11-10T15:31:02-04:00";
    m.width = 640;
    m.height = 480;
    m.orientation_q = std::array<double, 4>{1, 0, 0, 0};
    return m;
  }

  // 2026-11-10 15:30:00 hora local
  static std::chrono::system_clock::time_point fixedTime() {
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 10;
    tm.tm_mday = 10;
    tm.tm_hour = 15;
    tm.tm_min = 30;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
  }

  fs::path root_;
};

}  // namespace

TEST(SessionId, FormatsTimestampAndSanitizedName) {
  EXPECT_EQ(sanitizeSessionName("estatua01"), "estatua01");
  EXPECT_EQ(sanitizeSessionName("Mi taza/../x"), "Mi_taza____x");
  EXPECT_EQ(sanitizeSessionName(""), "");
  EXPECT_EQ(sanitizeSessionName(std::string(100, 'a')).size(), 40u);
  EXPECT_TRUE(isValidSessionId("20261110-153000-estatua01"));
  EXPECT_FALSE(isValidSessionId("../etc"));
  EXPECT_FALSE(isValidSessionId(""));
}

TEST_F(SessionStoreTest, CreateSessionMakesFolderWithTimestampedId) {
  SessionStore store(root_, QualityThresholds{});
  const std::string id = store.createSession("estatua01", json{{"model", "Pixel"}}, fixedTime());
  EXPECT_EQ(id, "20261110-153000-estatua01");
  EXPECT_TRUE(fs::is_directory(root_ / id));
  EXPECT_TRUE(store.exists(id));
  const Manifest m = store.manifest(id);
  EXPECT_EQ(m.device.at("model"), "Pixel");
  EXPECT_EQ(m.created_at.substr(0, 19), "2026-11-10T15:30:00");
}

TEST_F(SessionStoreTest, DuplicateIdsGetSuffix) {
  SessionStore store(root_, QualityThresholds{});
  const std::string a = store.createSession("x", json::object(), fixedTime());
  const std::string b = store.createSession("x", json::object(), fixedTime());
  EXPECT_NE(a, b);
  EXPECT_EQ(b, a + "-2");
}

TEST_F(SessionStoreTest, SessionWithoutNameUsesOnlyTimestamp) {
  SessionStore store(root_, QualityThresholds{});
  EXPECT_EQ(store.createSession("", json::object(), fixedTime()), "20261110-153000");
}

TEST_F(SessionStoreTest, AddPhotoSavesJpegAndValidates) {
  SessionStore store(root_, QualityThresholds{});
  const std::string id = store.createSession("s", json::object(), fixedTime());
  const PhotoRecord r = store.addPhoto(id, meta(1), testimg::sharpJpeg(640, 480));
  EXPECT_EQ(r.file, "img_001.jpg");
  EXPECT_TRUE(r.quality.accepted);
  EXPECT_TRUE(fs::exists(root_ / id / "img_001.jpg"));
  EXPECT_EQ(store.manifest(id).photos.size(), 1u);
}

TEST_F(SessionStoreTest, ServerDimensionsOverrideDeclaredOnes) {
  SessionStore store(root_, QualityThresholds{});
  const std::string id = store.createSession("s", json::object(), fixedTime());
  PhotoMetadata m = meta(1);
  m.width = 4000;
  m.height = 3000;
  const PhotoRecord r = store.addPhoto(id, m, testimg::sharpJpeg(320, 240));
  EXPECT_EQ(r.metadata.width, 320);
  EXPECT_EQ(r.metadata.height, 240);
}

TEST_F(SessionStoreTest, BlurryPhotoIsRejected) {
  SessionStore store(root_, QualityThresholds{});
  const std::string id = store.createSession("s", json::object(), fixedTime());
  const PhotoRecord r = store.addPhoto(id, meta(7), testimg::blurryJpeg(640, 480));
  EXPECT_FALSE(r.quality.accepted);
  EXPECT_EQ(r.quality.reasons, std::vector<std::string>{"borrosa"});
}

TEST_F(SessionStoreTest, SameIndexReplacesPhoto) {
  SessionStore store(root_, QualityThresholds{});
  const std::string id = store.createSession("s", json::object(), fixedTime());
  store.addPhoto(id, meta(2), testimg::blurryJpeg(640, 480));
  const PhotoRecord r = store.addPhoto(id, meta(2), testimg::sharpJpeg(640, 480));
  EXPECT_TRUE(r.quality.accepted);
  const Manifest m = store.manifest(id);
  ASSERT_EQ(m.photos.size(), 1u);
  EXPECT_TRUE(m.photos[0].quality.accepted);
}

TEST_F(SessionStoreTest, PhotosAreKeptOrderedByIndex) {
  SessionStore store(root_, QualityThresholds{});
  const std::string id = store.createSession("s", json::object(), fixedTime());
  store.addPhoto(id, meta(3), testimg::sharpJpeg(64, 48));
  store.addPhoto(id, meta(1), testimg::sharpJpeg(64, 48));
  store.addPhoto(id, meta(2), testimg::sharpJpeg(64, 48));
  const Manifest m = store.manifest(id);
  ASSERT_EQ(m.photos.size(), 3u);
  EXPECT_EQ(m.photos[0].metadata.index, 1);
  EXPECT_EQ(m.photos[2].metadata.index, 3);
}

TEST_F(SessionStoreTest, CorruptJpegThrowsAndStoresNothing) {
  SessionStore store(root_, QualityThresholds{});
  const std::string id = store.createSession("s", json::object(), fixedTime());
  EXPECT_THROW(store.addPhoto(id, meta(1), "esto no es un jpeg"), InvalidImageError);
  EXPECT_FALSE(fs::exists(root_ / id / "img_001.jpg"));
  EXPECT_TRUE(store.manifest(id).photos.empty());
}

TEST_F(SessionStoreTest, CompleteWritesManifestAndReturnsSummary) {
  SessionStore store(root_, QualityThresholds{});
  const std::string id = store.createSession("s", json{{"model", "Pixel"}}, fixedTime());
  store.addPhoto(id, meta(1), testimg::sharpJpeg(640, 480));
  store.addPhoto(id, meta(2), testimg::blurryJpeg(640, 480));
  const Summary s = store.complete(id);
  EXPECT_EQ(s.total, 2);
  EXPECT_EQ(s.accepted, 1);
  EXPECT_EQ(s.rejected, std::vector<int>{2});

  const fs::path file = root_ / id / "manifest.json";
  ASSERT_TRUE(fs::exists(file));
  const Manifest m = readManifest(file);
  EXPECT_EQ(m.session_id, id);
  EXPECT_EQ(m.photos.size(), 2u);
}

TEST_F(SessionStoreTest, CanRetakeAfterCompleteAndCompleteAgain) {
  SessionStore store(root_, QualityThresholds{});
  const std::string id = store.createSession("s", json::object(), fixedTime());
  store.addPhoto(id, meta(1), testimg::blurryJpeg(640, 480));
  EXPECT_EQ(store.complete(id).accepted, 0);
  store.addPhoto(id, meta(1), testimg::sharpJpeg(640, 480));
  EXPECT_EQ(store.complete(id).accepted, 1);
  EXPECT_EQ(readManifest(root_ / id / "manifest.json").photos[0].quality.accepted, true);
}

TEST_F(SessionStoreTest, UnknownSessionThrowsNotFound) {
  SessionStore store(root_, QualityThresholds{});
  EXPECT_FALSE(store.exists("20000101-000000-nada"));
  EXPECT_THROW(store.addPhoto("20000101-000000-nada", meta(1), testimg::sharpJpeg(64, 48)),
               SessionNotFoundError);
  EXPECT_THROW(store.complete("20000101-000000-nada"), SessionNotFoundError);
  EXPECT_THROW(store.manifest("20000101-000000-nada"), SessionNotFoundError);
}
