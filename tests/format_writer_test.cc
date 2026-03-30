#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstring>
#include <filesystem>
#include <vector>

#include "cdbapi/cdbapi.h"
#include "cdbapi/format/geopackage_writer.h"
#include "cdbapi/format/jpeg2000_writer.h"
#include "cdbapi/format/png_writer.h"

#include "gdal.h"

namespace fs = std::filesystem;

namespace {

auto TempDir(const char* name) -> fs::path {
  auto dir = fs::temp_directory_path() / "cdbapi_test" / name;
  fs::create_directories(dir);
  return dir;
}

void CleanDir(const fs::path& dir) {
  std::error_code ec;
  fs::remove_all(dir, ec);
}

}  // namespace

// --- GeoPackage Writer ---

TEST_CASE("GeoPackageWriter writes a single-band float32 raster",
          "[format][geopackage]") {
  auto dir = TempDir("gpkg_single");
  auto path = dir / "test.gpkg";

  constexpr int kWidth = 64;
  constexpr int kHeight = 64;
  constexpr int kBands = 1;
  using PT = cdbapi::coverage::PixelType;

  std::vector<float> values(kWidth * kHeight);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    values[i] = static_cast<float>(i) * 0.5f;
  }
  std::vector<std::byte> pixels(values.size() * sizeof(float));
  std::memcpy(pixels.data(), values.data(), pixels.size());

  cdbapi::tiling::GeoBounds bounds{32.0, -118.0, 33.0, -117.0};
  cdbapi::coverage::CoverageMetadata meta{
      .field_name = "elevation",
      .data_null = -32767.0};
  cdbapi::crs::Crs crs(4326);

  cdbapi::format::GeoPackageWriter writer;
  auto result = writer.WriteTile(path, pixels, kWidth, kHeight, kBands,
                                 PT::kFloat32, bounds, meta, crs);
  REQUIRE(result.has_value());
  REQUIRE(fs::exists(path));
  CHECK(fs::file_size(path) > 0);

  // Read back with GDAL.
  GDALAllRegister();
  GDALDatasetH ds = GDALOpen(path.string().c_str(), GA_ReadOnly);
  REQUIRE(ds != nullptr);
  CHECK(GDALGetRasterXSize(ds) == kWidth);
  CHECK(GDALGetRasterYSize(ds) == kHeight);
  CHECK(GDALGetRasterCount(ds) == kBands);

  double gt[6];
  REQUIRE(GDALGetGeoTransform(ds, gt) == CE_None);
  CHECK(gt[0] == Catch::Approx(-118.0));
  CHECK(gt[3] == Catch::Approx(33.0));

  // Read first pixel.
  float val = 0.0f;
  GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Float32, 0, 0);
  CHECK(val == Catch::Approx(0.0f));

  GDALClose(ds);
  CleanDir(dir);
}

TEST_CASE("GeoPackageWriter writes a 3-band uint8 raster",
          "[format][geopackage]") {
  auto dir = TempDir("gpkg_rgb");
  auto path = dir / "test_rgb.gpkg";

  constexpr int kWidth = 32;
  constexpr int kHeight = 32;
  constexpr int kBands = 3;
  using PT = cdbapi::coverage::PixelType;

  std::vector<std::byte> pixels(kWidth * kHeight * kBands);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    pixels[i * 3 + 0] = std::byte{80};
    pixels[i * 3 + 1] = std::byte{160};
    pixels[i * 3 + 2] = std::byte{240};
  }

  cdbapi::tiling::GeoBounds bounds{0.0, 0.0, 1.0, 1.0};
  cdbapi::coverage::CoverageMetadata meta{
      .field_name = "imagery", .data_null = 0.0};
  cdbapi::crs::Crs crs(4326);

  cdbapi::format::GeoPackageWriter writer;
  auto result = writer.WriteTile(path, pixels, kWidth, kHeight, kBands,
                                 PT::kUint8, bounds, meta, crs);
  REQUIRE(result.has_value());

  GDALAllRegister();
  GDALDatasetH ds = GDALOpen(path.string().c_str(), GA_ReadOnly);
  REQUIRE(ds != nullptr);
  // GPKG with PNG tiles may add an alpha band for RGB data.
  CHECK(GDALGetRasterCount(ds) >= 3);

  uint8_t val = 0;
  GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 80);
  GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 160);
  GDALRasterIO(GDALGetRasterBand(ds, 3), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 240);

  GDALClose(ds);
  CleanDir(dir);
}

// --- JPEG2000 Writer ---

TEST_CASE("Jpeg2000Writer writes a single-band uint16 raster",
          "[format][jpeg2000]") {
  auto dir = TempDir("jp2_single");
  auto path = dir / "test.jp2";

  constexpr int kWidth = 64;
  constexpr int kHeight = 64;
  constexpr int kBands = 1;
  using PT = cdbapi::coverage::PixelType;

  std::vector<uint16_t> values(kWidth * kHeight);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    values[i] = static_cast<uint16_t>(i % 65536);
  }
  std::vector<std::byte> pixels(values.size() * sizeof(uint16_t));
  std::memcpy(pixels.data(), values.data(), pixels.size());

  cdbapi::tiling::GeoBounds bounds{32.0, -118.0, 33.0, -117.0};
  cdbapi::coverage::CoverageMetadata meta{
      .field_name = "elevation",
      .data_null = 0.0};
  cdbapi::crs::Crs crs(4326);

  cdbapi::format::Jpeg2000Writer writer;
  auto result = writer.WriteTile(path, pixels, kWidth, kHeight, kBands,
                                 PT::kUint16, bounds, meta, crs);
  REQUIRE(result.has_value());
  REQUIRE(fs::exists(path));
  CHECK(fs::file_size(path) > 0);

  // Read back — lossless, so values must match exactly.
  GDALAllRegister();
  GDALDatasetH ds = GDALOpen(path.string().c_str(), GA_ReadOnly);
  REQUIRE(ds != nullptr);
  CHECK(GDALGetRasterXSize(ds) == kWidth);
  CHECK(GDALGetRasterYSize(ds) == kHeight);

  double gt[6];
  REQUIRE(GDALGetGeoTransform(ds, gt) == CE_None);
  CHECK(gt[0] == Catch::Approx(-118.0));
  CHECK(gt[3] == Catch::Approx(33.0));

  uint16_t val = 0;
  GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_UInt16, 0, 0);
  CHECK(val == 0);

  // Check pixel (1,0): value should be 1.
  GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 1, 0, 1, 1,
               &val, 1, 1, GDT_UInt16, 0, 0);
  CHECK(val == 1);

  GDALClose(ds);
  CleanDir(dir);
}

TEST_CASE("Jpeg2000Writer writes a 3-band uint8 raster",
          "[format][jpeg2000]") {
  auto dir = TempDir("jp2_rgb");
  auto path = dir / "test_rgb.jp2";

  constexpr int kWidth = 32;
  constexpr int kHeight = 32;
  constexpr int kBands = 3;
  using PT = cdbapi::coverage::PixelType;

  std::vector<std::byte> pixels(kWidth * kHeight * kBands);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    pixels[i * 3 + 0] = std::byte{50};
    pixels[i * 3 + 1] = std::byte{100};
    pixels[i * 3 + 2] = std::byte{200};
  }

  cdbapi::tiling::GeoBounds bounds{0.0, 0.0, 1.0, 1.0};
  cdbapi::coverage::CoverageMetadata meta{
      .field_name = "imagery", .data_null = 0.0};
  cdbapi::crs::Crs crs(4326);

  cdbapi::format::Jpeg2000Writer writer;
  auto result = writer.WriteTile(path, pixels, kWidth, kHeight, kBands,
                                 PT::kUint8, bounds, meta, crs);
  REQUIRE(result.has_value());

  GDALAllRegister();
  GDALDatasetH ds = GDALOpen(path.string().c_str(), GA_ReadOnly);
  REQUIRE(ds != nullptr);
  CHECK(GDALGetRasterCount(ds) == 3);

  // Lossless: values must match.
  uint8_t val = 0;
  GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 50);
  GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 100);
  GDALRasterIO(GDALGetRasterBand(ds, 3), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 200);

  GDALClose(ds);
  CleanDir(dir);
}

// --- PNG Writer ---

TEST_CASE("PngWriter writes a single-band uint8 raster",
          "[format][png]") {
  auto dir = TempDir("png_single");
  auto path = dir / "test.png";

  constexpr int kWidth = 64;
  constexpr int kHeight = 64;
  constexpr int kBands = 1;
  using PT = cdbapi::coverage::PixelType;

  std::vector<std::byte> pixels(kWidth * kHeight);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    pixels[i] = std::byte(i % 256);
  }

  cdbapi::tiling::GeoBounds bounds{32.0, -118.0, 33.0, -117.0};
  cdbapi::coverage::CoverageMetadata meta{
      .field_name = "mask",
      .data_null = 0.0};
  cdbapi::crs::Crs crs(4326);

  cdbapi::format::PngWriter writer;
  auto result = writer.WriteTile(path, pixels, kWidth, kHeight, kBands,
                                 PT::kUint8, bounds, meta, crs);
  REQUIRE(result.has_value());
  REQUIRE(fs::exists(path));
  CHECK(fs::file_size(path) > 0);

  // Read back.
  GDALAllRegister();
  GDALDatasetH ds = GDALOpen(path.string().c_str(), GA_ReadOnly);
  REQUIRE(ds != nullptr);
  CHECK(GDALGetRasterXSize(ds) == kWidth);
  CHECK(GDALGetRasterYSize(ds) == kHeight);

  uint8_t val = 255;
  GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 0);

  GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 5, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 5);

  GDALClose(ds);
  CleanDir(dir);
}

TEST_CASE("PngWriter writes a uint16 raster",
          "[format][png]") {
  auto dir = TempDir("png_uint16");
  auto path = dir / "test16.png";

  constexpr int kWidth = 32;
  constexpr int kHeight = 32;
  constexpr int kBands = 1;
  using PT = cdbapi::coverage::PixelType;

  std::vector<uint16_t> values(kWidth * kHeight);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    values[i] = static_cast<uint16_t>(i * 100);
  }
  std::vector<std::byte> pixels(values.size() * sizeof(uint16_t));
  std::memcpy(pixels.data(), values.data(), pixels.size());

  cdbapi::tiling::GeoBounds bounds{0.0, 0.0, 1.0, 1.0};
  cdbapi::coverage::CoverageMetadata meta{
      .field_name = "depth", .data_null = 0.0};
  cdbapi::crs::Crs crs(4326);

  cdbapi::format::PngWriter writer;
  auto result = writer.WriteTile(path, pixels, kWidth, kHeight, kBands,
                                 PT::kUint16, bounds, meta, crs);
  REQUIRE(result.has_value());

  GDALAllRegister();
  GDALDatasetH ds = GDALOpen(path.string().c_str(), GA_ReadOnly);
  REQUIRE(ds != nullptr);

  uint16_t val = 0;
  GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_UInt16, 0, 0);
  CHECK(val == 0);
  GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 1, 0, 1, 1,
               &val, 1, 1, GDT_UInt16, 0, 0);
  CHECK(val == 100);

  GDALClose(ds);
  CleanDir(dir);
}

TEST_CASE("PngWriter rejects float32 pixel type",
          "[format][png]") {
  auto dir = TempDir("png_reject_float");
  auto path = dir / "test_bad.png";

  constexpr int kSize = 8;
  using PT = cdbapi::coverage::PixelType;

  std::vector<std::byte> pixels(kSize * kSize * sizeof(float), std::byte{0});

  cdbapi::tiling::GeoBounds bounds{0.0, 0.0, 1.0, 1.0};
  cdbapi::coverage::CoverageMetadata meta{.field_name = "elev"};
  cdbapi::crs::Crs crs(4326);

  cdbapi::format::PngWriter writer;
  auto result = writer.WriteTile(path, pixels, kSize, kSize, 1,
                                 PT::kFloat32, bounds, meta, crs);
  CHECK_FALSE(result.has_value());

  CleanDir(dir);
}
