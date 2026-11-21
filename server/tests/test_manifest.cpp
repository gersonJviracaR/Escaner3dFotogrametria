// Pruebas de HU-05/HU-02: metadatos por foto y manifiesto de la sesión.
#include "manifest.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

json sampleMetadataJson() {
  return json::parse(R"({
    "index": 1,
    "captured_at": "2026-11-10T15:31:02-04:00",
    "width": 4000, "height": 3000,
    "focal_mm": 5.6, "focal_35mm": 24,
    "orientation_q": [0.98, 0.01, 0.17, 0.02]
  })");
}

Manifest sampleManifest() {
  Manifest m;
  m.session_id = "20261110-153000-estatua01";
  m.created_at = "2026-11-10T15:30:00-04:00";
  m.device = json{{"model", "Pixel 7"}, {"android", "14"}};

  PhotoRecord a;
  a.metadata = parsePhotoMetadata(sampleMetadataJson());
  a.file = "img_001.jpg";
  a.quality = QualityResult{152.3, 0.02, true, {}};

  PhotoRecord b;
  b.metadata.index = 7;
  b.metadata.width = 4000;
  b.metadata.height = 3000;
  b.file = "img_007.jpg";
  b.quality = QualityResult{41.2, 0.01, false, {"borrosa"}};

  m.photos = {a, b};
  return m;
}

}  // namespace

TEST(PhotoMetadata, ParsesAllFields) {
  const PhotoMetadata m = parsePhotoMetadata(sampleMetadataJson());
  EXPECT_EQ(m.index, 1);
  EXPECT_EQ(m.captured_at, "2026-11-10T15:31:02-04:00");
  EXPECT_EQ(m.width, 4000);
  EXPECT_EQ(m.height, 3000);
  ASSERT_TRUE(m.focal_mm.has_value());
  EXPECT_DOUBLE_EQ(*m.focal_mm, 5.6);
  ASSERT_TRUE(m.focal_35mm.has_value());
  EXPECT_DOUBLE_EQ(*m.focal_35mm, 24.0);
  ASSERT_TRUE(m.orientation_q.has_value());
  EXPECT_DOUBLE_EQ((*m.orientation_q)[2], 0.17);
}

TEST(PhotoMetadata, AcceptsNullFocalLengths) {
  json j = sampleMetadataJson();
  j["focal_mm"] = nullptr;
  j.erase("focal_35mm");
  const PhotoMetadata m = parsePhotoMetadata(j);
  EXPECT_FALSE(m.focal_mm.has_value());
  EXPECT_FALSE(m.focal_35mm.has_value());
  const json back = toJson(m);
  EXPECT_TRUE(back.at("focal_mm").is_null());
  EXPECT_TRUE(back.at("focal_35mm").is_null());
}

TEST(PhotoMetadata, RejectsMissingOrInvalidIndex) {
  json j = sampleMetadataJson();
  j.erase("index");
  EXPECT_THROW(parsePhotoMetadata(j), MetadataError);
  j["index"] = 0;
  EXPECT_THROW(parsePhotoMetadata(j), MetadataError);
  j["index"] = 1000;
  EXPECT_THROW(parsePhotoMetadata(j), MetadataError);
  j["index"] = "uno";
  EXPECT_THROW(parsePhotoMetadata(j), MetadataError);
}

TEST(PhotoMetadata, RejectsMalformedQuaternion) {
  json j = sampleMetadataJson();
  j["orientation_q"] = json::array({1.0, 0.0, 0.0});
  EXPECT_THROW(parsePhotoMetadata(j), MetadataError);
  j["orientation_q"] = "w,x,y,z";
  EXPECT_THROW(parsePhotoMetadata(j), MetadataError);
}

TEST(PhotoMetadata, RejectsNonObject) {
  EXPECT_THROW(parsePhotoMetadata(json::array()), MetadataError);
}

TEST(PhotoFileName, IsZeroPadded) {
  EXPECT_EQ(photoFileName(1), "img_001.jpg");
  EXPECT_EQ(photoFileName(20), "img_020.jpg");
  EXPECT_EQ(photoFileName(999), "img_999.jpg");
}

TEST(Summary, CountsAcceptedAndListsRejectedSorted) {
  Manifest m = sampleManifest();
  std::swap(m.photos[0], m.photos[1]);
  const Summary s = summarize(m.photos);
  EXPECT_EQ(s.total, 2);
  EXPECT_EQ(s.accepted, 1);
  EXPECT_EQ(s.rejected, std::vector<int>{7});
}

TEST(Manifest, JsonHasExpectedShape) {
  const json j = toJson(sampleManifest());
  EXPECT_EQ(j.at("schema_version"), 1);
  EXPECT_EQ(j.at("session_id"), "20261110-153000-estatua01");
  EXPECT_EQ(j.at("device").at("model"), "Pixel 7");
  ASSERT_EQ(j.at("photos").size(), 2u);
  const json& p = j.at("photos").at(0);
  EXPECT_EQ(p.at("file"), "img_001.jpg");
  EXPECT_EQ(p.at("quality").at("accepted"), true);
  EXPECT_TRUE(p.at("quality").at("reasons").is_array());
  EXPECT_TRUE(j.at("photos").at(1).at("orientation_q").is_null());
  EXPECT_EQ(j.at("summary").at("total"), 2);
  EXPECT_EQ(j.at("summary").at("accepted"), 1);
  EXPECT_EQ(j.at("summary").at("rejected"), json::array({7}));
}

TEST(Manifest, RoundTripThroughJson) {
  const Manifest original = sampleManifest();
  const Manifest back = manifestFromJson(toJson(original));
  EXPECT_EQ(toJson(back), toJson(original));
  EXPECT_FALSE(back.photos[1].metadata.focal_mm.has_value());
}

TEST(Manifest, AtomicWriteThenRead) {
  const fs::path dir = fs::temp_directory_path() / "escaner3d_test_manifest";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path file = dir / "manifest.json";

  writeManifestAtomic(file, sampleManifest());
  EXPECT_TRUE(fs::exists(file));
  EXPECT_FALSE(fs::exists(dir / "manifest.json.tmp"));
  EXPECT_EQ(toJson(readManifest(file)), toJson(sampleManifest()));

  // Sobrescribir un manifiesto existente también funciona.
  Manifest changed = sampleManifest();
  changed.photos.pop_back();
  writeManifestAtomic(file, changed);
  EXPECT_EQ(readManifest(file).photos.size(), 1u);

  fs::remove_all(dir);
}
