// Pruebas de la matriz intrínseca K inicial (sección 3.3 del Sprint 2).
#include "intrinsics.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

PhotoMetadata photo(int width, int height, std::optional<double> focal35) {
  PhotoMetadata m;
  m.index = 1;
  m.width = width;
  m.height = height;
  m.focal_35mm = focal35;
  return m;
}

}  // namespace

TEST(Intrinsics, FromFocal35mmUsesImageDiagonal) {
  const CameraIntrinsics k = intrinsicsFromMetadata(photo(4000, 3000, 25.0), 1.2);
  EXPECT_NEAR(k.focal_px, 25.0 * 5000.0 / 43.27, 1e-9);  // ≈ 2889 px
  EXPECT_NEAR(k.focal_px, 2889.0, 0.5);
  EXPECT_DOUBLE_EQ(k.cx, 2000.0);
  EXPECT_DOUBLE_EQ(k.cy, 1500.0);
  EXPECT_FALSE(k.used_fallback);

  const cv::Matx33d K = k.matrix();
  EXPECT_DOUBLE_EQ(K(0, 0), k.focal_px);
  EXPECT_DOUBLE_EQ(K(1, 1), k.focal_px);
  EXPECT_DOUBLE_EQ(K(0, 1), 0.0);  // sin sesgo
  EXPECT_DOUBLE_EQ(K(0, 2), 2000.0);
  EXPECT_DOUBLE_EQ(K(1, 2), 1500.0);
  EXPECT_DOUBLE_EQ(K(2, 2), 1.0);
  EXPECT_DOUBLE_EQ(K(2, 0), 0.0);
}

TEST(Intrinsics, PortraitImageGivesSameFocal) {
  const CameraIntrinsics land = intrinsicsFromMetadata(photo(4000, 3000, 25.0), 1.2);
  const CameraIntrinsics port = intrinsicsFromMetadata(photo(3000, 4000, 25.0), 1.2);
  EXPECT_DOUBLE_EQ(land.focal_px, port.focal_px);
  EXPECT_DOUBLE_EQ(port.cx, 1500.0);
  EXPECT_DOUBLE_EQ(port.cy, 2000.0);
}

TEST(Intrinsics, FallbackWhenFocal35mmIsMissing) {
  const CameraIntrinsics k = intrinsicsFromMetadata(photo(4000, 3000, std::nullopt), 1.2);
  EXPECT_DOUBLE_EQ(k.focal_px, 4800.0);
  EXPECT_TRUE(k.used_fallback);
  // Una focal no positiva también se trata como ausente.
  EXPECT_TRUE(intrinsicsFromMetadata(photo(4000, 3000, 0.0), 1.2).used_fallback);
}

TEST(Intrinsics, MissingDimensionsThrow) {
  EXPECT_THROW(intrinsicsFromMetadata(photo(0, 3000, 25.0), 1.2), std::invalid_argument);
}
