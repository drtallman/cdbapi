#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstring>
#include <filesystem>
#include <vector>

#include "cdbapi/cdbapi.h"
#include "cdbapi/format/geotiff_writer.h"

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

TEST_CASE("GeoTiffWriter writes a single-band GeoTIFF", "[coverage][geotiff]") {
  auto dir = TempDir("geotiff_single");
  auto path = dir / "test_single.tif";

  constexpr int kWidth = 64;
  constexpr int kHeight = 64;
  constexpr int kBands = 1;
  using PT = cdbapi::coverage::PixelType;

  // Fill with a linear ramp.
  std::vector<float> values(kWidth * kHeight);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    values[i] = static_cast<float>(i);
  }

  std::vector<std::byte> pixels(values.size() * sizeof(float));
  std::memcpy(pixels.data(), values.data(), pixels.size());

  cdbapi::tiling::GeoBounds bounds{32.0, -118.0, 33.0, -117.0};
  cdbapi::coverage::CoverageMetadata meta{
      .field_name = "elevation",
      .quantity_definition = "height above WGS84 ellipsoid",
      .data_null = -32767.0};
  cdbapi::crs::Crs crs(4326);

  cdbapi::format::GeoTiffWriter writer;
  auto result = writer.WriteTile(path, pixels, kWidth, kHeight, kBands,
                                 PT::kFloat32, bounds, meta, crs);
  REQUIRE(result.has_value());
  REQUIRE(fs::exists(path));
  CHECK(fs::file_size(path) > 0);

  // Read back with GDAL and verify.
  GDALAllRegister();
  GDALDatasetH ds = GDALOpen(path.string().c_str(), GA_ReadOnly);
  REQUIRE(ds != nullptr);

  CHECK(GDALGetRasterXSize(ds) == kWidth);
  CHECK(GDALGetRasterYSize(ds) == kHeight);
  CHECK(GDALGetRasterCount(ds) == kBands);

  double gt[6];
  REQUIRE(GDALGetGeoTransform(ds, gt) == CE_None);
  CHECK(gt[0] == Catch::Approx(-118.0));  // west
  CHECK(gt[3] == Catch::Approx(33.0));    // north
  CHECK(gt[1] == Catch::Approx(1.0 / kWidth));
  CHECK(gt[5] == Catch::Approx(-1.0 / kHeight));

  GDALRasterBandH band = GDALGetRasterBand(ds, 1);
  int has_nodata = 0;
  double nodata = GDALGetRasterNoDataValue(band, &has_nodata);
  CHECK(has_nodata);
  CHECK(nodata == Catch::Approx(-32767.0));

  // Read back first pixel.
  float pixel_val = 0.0f;
  GDALRasterIO(band, GF_Read, 0, 0, 1, 1, &pixel_val, 1, 1,
               GDT_Float32, 0, 0);
  CHECK(pixel_val == Catch::Approx(0.0f));

  GDALClose(ds);
  CleanDir(dir);
}

TEST_CASE("GeoTiffWriter writes a 3-band uint8 GeoTIFF", "[coverage][geotiff]") {
  auto dir = TempDir("geotiff_multiband");
  auto path = dir / "test_rgb.tif";

  constexpr int kWidth = 32;
  constexpr int kHeight = 32;
  constexpr int kBands = 3;
  using PT = cdbapi::coverage::PixelType;

  // BIP layout: R G B R G B ...
  std::vector<std::byte> pixels(kWidth * kHeight * kBands);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    pixels[i * 3 + 0] = std::byte{100};  // R
    pixels[i * 3 + 1] = std::byte{150};  // G
    pixels[i * 3 + 2] = std::byte{200};  // B
  }

  cdbapi::tiling::GeoBounds bounds{0.0, 0.0, 1.0, 1.0};
  cdbapi::coverage::CoverageMetadata meta{
      .field_name = "imagery",
      .data_null = 0.0};
  cdbapi::crs::Crs crs(4326);

  cdbapi::format::GeoTiffWriter writer;
  auto result = writer.WriteTile(path, pixels, kWidth, kHeight, kBands,
                                 PT::kUint8, bounds, meta, crs);
  REQUIRE(result.has_value());

  // Read back and verify per-band values.
  GDALAllRegister();
  GDALDatasetH ds = GDALOpen(path.string().c_str(), GA_ReadOnly);
  REQUIRE(ds != nullptr);
  CHECK(GDALGetRasterCount(ds) == 3);

  uint8_t val = 0;
  GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 100);
  GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 150);
  GDALRasterIO(GDALGetRasterBand(ds, 3), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Byte, 0, 0);
  CHECK(val == 200);

  GDALClose(ds);
  CleanDir(dir);
}

TEST_CASE("WriteCoverage creates correct directory structure",
          "[coverage][datastore]") {
  auto root = TempDir("datastore_single");

  cdbapi::metadata::DatastoreMetadata meta{
      .id = "test-ds",
      .title = "Test Datastore",
      .description = "Unit test",
      .contact_point = "test@test.com",
      .created = "2026-03-28"};

  auto ds_result = cdbapi::CdbDatastore::Create(
      root, cdbapi::DefaultProfile(), meta);
  REQUIRE(ds_result.has_value());
  auto& ds = ds_result.value();

  // 1024x1024 float32 at LOD 0, single geocell.
  constexpr int kSize = 1024;
  std::vector<std::byte> pixels(kSize * kSize * sizeof(float), std::byte{0});

  cdbapi::coverage::CoverageDescriptor desc{
      .dataset_name = "elevation",
      .bounds = {32.0, -118.0, 33.0, -117.0},
      .target_lod = 0,
      .metadata = {.field_name = "elevation",
                   .quantity_definition = "height",
                   .data_null = -32767.0}};
  cdbapi::coverage::CoverageData data{
      .width = kSize, .height = kSize, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  auto result = ds.WriteCoverage(desc, data);
  REQUIRE(result.has_value());

  auto tile_path = root / "coverages" / "elevation" / "lod_00" / "n32_w118.tif";
  CHECK(fs::exists(tile_path));
  CHECK(fs::file_size(tile_path) > 0);

  auto fin = ds.Finalize();
  REQUIRE(fin.has_value());

  CleanDir(root);
}

TEST_CASE("WriteCoverage tiles across multiple geocells",
          "[coverage][datastore]") {
  auto root = TempDir("datastore_multi");

  cdbapi::metadata::DatastoreMetadata meta{
      .id = "test-ds",
      .title = "Test",
      .description = "Multi-cell",
      .contact_point = "test@test.com",
      .created = "2026-03-28"};

  auto ds_result = cdbapi::CdbDatastore::Create(
      root, cdbapi::DefaultProfile(), meta);
  REQUIRE(ds_result.has_value());
  auto& ds = ds_result.value();

  // 2x2 geocells at LOD 0 → 2048x2048 pixels.
  constexpr int kSize = 2048;
  std::vector<std::byte> pixels(
      static_cast<size_t>(kSize) * kSize * sizeof(float), std::byte{0});

  cdbapi::coverage::CoverageDescriptor desc{
      .dataset_name = "elevation",
      .bounds = {32.0, -118.0, 34.0, -116.0},
      .target_lod = 0,
      .metadata = {.field_name = "elevation", .data_null = -32767.0}};
  cdbapi::coverage::CoverageData data{
      .width = kSize, .height = kSize, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  auto result = ds.WriteCoverage(desc, data);
  REQUIRE(result.has_value());

  auto base = root / "coverages" / "elevation" / "lod_00";
  CHECK(fs::exists(base / "n32_w118.tif"));
  CHECK(fs::exists(base / "n32_w117.tif"));
  CHECK(fs::exists(base / "n33_w118.tif"));
  CHECK(fs::exists(base / "n33_w117.tif"));

  CleanDir(root);
}

TEST_CASE("WriteCoverage at LOD 1 creates sub-tiles",
          "[coverage][datastore]") {
  auto root = TempDir("datastore_lod1");

  cdbapi::metadata::DatastoreMetadata meta{
      .id = "test-ds",
      .title = "Test",
      .description = "LOD 1",
      .contact_point = "test@test.com",
      .created = "2026-03-28"};

  auto ds_result = cdbapi::CdbDatastore::Create(
      root, cdbapi::DefaultProfile(), meta);
  REQUIRE(ds_result.has_value());
  auto& ds = ds_result.value();

  // LOD 1: 2x2 tiles per geocell, each 1024x1024 → source 2048x2048.
  constexpr int kSize = 2048;
  std::vector<std::byte> pixels(
      static_cast<size_t>(kSize) * kSize * sizeof(float), std::byte{0});

  cdbapi::coverage::CoverageDescriptor desc{
      .dataset_name = "terrain",
      .bounds = {32.0, -118.0, 33.0, -117.0},
      .target_lod = 1,
      .metadata = {.field_name = "terrain", .data_null = -32767.0}};
  cdbapi::coverage::CoverageData data{
      .width = kSize, .height = kSize, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  auto result = ds.WriteCoverage(desc, data);
  REQUIRE(result.has_value());

  auto base = root / "coverages" / "terrain" / "lod_01";
  CHECK(fs::exists(base / "n32_w118_r0_c0.tif"));
  CHECK(fs::exists(base / "n32_w118_r0_c1.tif"));
  CHECK(fs::exists(base / "n32_w118_r1_c0.tif"));
  CHECK(fs::exists(base / "n32_w118_r1_c1.tif"));

  CleanDir(root);
}

TEST_CASE("WriteCoverage rejects invalid inputs",
          "[coverage][datastore]") {
  auto root = TempDir("datastore_invalid");

  cdbapi::metadata::DatastoreMetadata meta{
      .id = "test-ds",
      .title = "Test",
      .description = "Validation",
      .contact_point = "test@test.com",
      .created = "2026-03-28"};

  auto ds_result = cdbapi::CdbDatastore::Create(
      root, cdbapi::DefaultProfile(), meta);
  REQUIRE(ds_result.has_value());
  auto& ds = ds_result.value();

  cdbapi::coverage::CoverageMetadata cmeta{
      .field_name = "elev", .data_null = -32767.0};
  std::vector<std::byte> pixels(1024 * 1024 * 4, std::byte{0});

  SECTION("Empty dataset name") {
    cdbapi::coverage::CoverageDescriptor desc{
        .dataset_name = "",
        .bounds = {32.0, -118.0, 33.0, -117.0},
        .target_lod = 0,
        .metadata = cmeta};
    cdbapi::coverage::CoverageData data{
        .width = 1024, .height = 1024,
        .pixel_type = cdbapi::coverage::PixelType::kFloat32,
        .pixels = pixels};
    CHECK_FALSE(ds.WriteCoverage(desc, data).has_value());
  }

  SECTION("Invalid bounds") {
    cdbapi::coverage::CoverageDescriptor desc{
        .dataset_name = "elev",
        .bounds = {33.0, -118.0, 32.0, -117.0},  // south > north
        .target_lod = 0,
        .metadata = cmeta};
    cdbapi::coverage::CoverageData data{
        .width = 1024, .height = 1024,
        .pixel_type = cdbapi::coverage::PixelType::kFloat32,
        .pixels = pixels};
    CHECK_FALSE(ds.WriteCoverage(desc, data).has_value());
  }

  SECTION("LOD out of range") {
    cdbapi::coverage::CoverageDescriptor desc{
        .dataset_name = "elev",
        .bounds = {32.0, -118.0, 33.0, -117.0},
        .target_lod = 99,
        .metadata = cmeta};
    cdbapi::coverage::CoverageData data{
        .width = 1024, .height = 1024,
        .pixel_type = cdbapi::coverage::PixelType::kFloat32,
        .pixels = pixels};
    CHECK_FALSE(ds.WriteCoverage(desc, data).has_value());
  }

  SECTION("Buffer size mismatch") {
    cdbapi::coverage::CoverageDescriptor desc{
        .dataset_name = "elev",
        .bounds = {32.0, -118.0, 33.0, -117.0},
        .target_lod = 0,
        .metadata = cmeta};
    std::vector<std::byte> small_pixels(100);
    cdbapi::coverage::CoverageData data{
        .width = 1024, .height = 1024,
        .pixel_type = cdbapi::coverage::PixelType::kFloat32,
        .pixels = small_pixels};
    CHECK_FALSE(ds.WriteCoverage(desc, data).has_value());
  }

  CleanDir(root);
}

// --- LOD Pyramid Tests ---

TEST_CASE("GenerateLodPyramid from LOD 1 to LOD 0",
          "[pyramid][datastore]") {
  auto root = TempDir("pyramid_lod1_to_0");

  cdbapi::metadata::DatastoreMetadata meta{
      .id = "test-ds",
      .title = "Pyramid Test",
      .description = "LOD 1 to 0",
      .contact_point = "test@test.com",
      .created = "2026-03-30"};
  auto ds_result = cdbapi::CdbDatastore::Create(
      root, cdbapi::DefaultProfile(), meta);
  REQUIRE(ds_result.has_value());
  auto& ds = ds_result.value();

  // Write 2048x2048 at LOD 1 (one geocell = 4 tiles of 1024x1024).
  // Fill with constant 10.0f for simplicity.
  constexpr int kSize = 2048;
  std::vector<float> values(kSize * kSize, 10.0f);
  std::vector<std::byte> pixels(values.size() * sizeof(float));
  std::memcpy(pixels.data(), values.data(), pixels.size());

  cdbapi::coverage::CoverageDescriptor desc{
      .dataset_name = "elevation",
      .bounds = {32.0, -118.0, 33.0, -117.0},
      .target_lod = 1,
      .metadata = {.field_name = "elevation",
                   .quantity_definition = "height",
                   .data_null = -32767.0}};
  cdbapi::coverage::CoverageData data{
      .width = kSize, .height = kSize, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  REQUIRE(ds.WriteCoverage(desc, data).has_value());

  // Verify LOD 1 tiles exist.
  auto base1 = root / "coverages" / "elevation" / "lod_01";
  REQUIRE(fs::exists(base1 / "n32_w118_r0_c0.tif"));
  REQUIRE(fs::exists(base1 / "n32_w118_r0_c1.tif"));
  REQUIRE(fs::exists(base1 / "n32_w118_r1_c0.tif"));
  REQUIRE(fs::exists(base1 / "n32_w118_r1_c1.tif"));

  // Generate pyramid to LOD 0.
  auto pyramid = ds.GenerateLodPyramid("elevation", 1, 0);
  REQUIRE(pyramid.has_value());

  // Verify LOD 0 tile exists with correct dimensions and value.
  auto lod0_path =
      root / "coverages" / "elevation" / "lod_00" / "n32_w118.tif";
  REQUIRE(fs::exists(lod0_path));

  GDALAllRegister();
  GDALDatasetH ds_read =
      GDALOpen(lod0_path.string().c_str(), GA_ReadOnly);
  REQUIRE(ds_read != nullptr);
  CHECK(GDALGetRasterXSize(ds_read) == 1024);
  CHECK(GDALGetRasterYSize(ds_read) == 1024);

  // All source pixels were 10.0, so average should also be 10.0.
  float val = 0.0f;
  GDALRasterIO(GDALGetRasterBand(ds_read, 1), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Float32, 0, 0);
  CHECK(val == Catch::Approx(10.0f));

  // Check center pixel too.
  GDALRasterIO(GDALGetRasterBand(ds_read, 1), GF_Read, 512, 512, 1, 1,
               &val, 1, 1, GDT_Float32, 0, 0);
  CHECK(val == Catch::Approx(10.0f));

  GDALClose(ds_read);
  CleanDir(root);
}

TEST_CASE("GenerateLodPyramid from LOD 0 to LOD -2 with pixel verification",
          "[pyramid][datastore]") {
  auto root = TempDir("pyramid_lod0_to_neg2");

  cdbapi::metadata::DatastoreMetadata meta{
      .id = "test-ds",
      .title = "Pyramid Neg Test",
      .description = "LOD 0 to -2",
      .contact_point = "test@test.com",
      .created = "2026-03-30"};
  auto ds_result = cdbapi::CdbDatastore::Create(
      root, cdbapi::DefaultProfile(), meta);
  REQUIRE(ds_result.has_value());
  auto& ds = ds_result.value();

  // Write 1024x1024 at LOD 0 with gradient: pixel[r][c] = r*1024+c.
  constexpr int kSize = 1024;
  std::vector<float> values(kSize * kSize);
  for (int i = 0; i < kSize * kSize; ++i) {
    values[i] = static_cast<float>(i);
  }
  std::vector<std::byte> pixels(values.size() * sizeof(float));
  std::memcpy(pixels.data(), values.data(), pixels.size());

  cdbapi::coverage::CoverageDescriptor desc{
      .dataset_name = "elevation",
      .bounds = {32.0, -118.0, 33.0, -117.0},
      .target_lod = 0,
      .metadata = {.field_name = "elevation",
                   .data_null = -32767.0}};
  cdbapi::coverage::CoverageData data{
      .width = kSize, .height = kSize, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  REQUIRE(ds.WriteCoverage(desc, data).has_value());

  // Generate pyramid from LOD 0 down to LOD -2.
  auto pyramid = ds.GenerateLodPyramid("elevation", 0, -2);
  REQUIRE(pyramid.has_value());

  GDALAllRegister();

  // LOD -1: should be 512x512.
  auto lod_neg1 = root / "coverages" / "elevation" / "lod_neg01"
                       / "n32_w118.tif";
  REQUIRE(fs::exists(lod_neg1));
  GDALDatasetH d1 = GDALOpen(lod_neg1.string().c_str(), GA_ReadOnly);
  REQUIRE(d1 != nullptr);
  CHECK(GDALGetRasterXSize(d1) == 512);
  CHECK(GDALGetRasterYSize(d1) == 512);

  // Pixel (0,0) at LOD -1 = avg of source (0,0),(0,1),(1,0),(1,1)
  // = avg(0, 1, 1024, 1025) = 2050/4 = 512.5
  float val = 0.0f;
  GDALRasterIO(GDALGetRasterBand(d1, 1), GF_Read, 0, 0, 1, 1,
               &val, 1, 1, GDT_Float32, 0, 0);
  CHECK(val == Catch::Approx(512.5f));

  // Pixel (0,1) at LOD -1 = avg of source (2,0),(2,1),(3,0),(3,1)
  // = avg(2048, 2049, 3072, 3073) = 10242/4 = 2560.5
  GDALRasterIO(GDALGetRasterBand(d1, 1), GF_Read, 0, 1, 1, 1,
               &val, 1, 1, GDT_Float32, 0, 0);
  CHECK(val == Catch::Approx(2560.5f));

  GDALClose(d1);

  // LOD -2: should be 256x256.
  auto lod_neg2 = root / "coverages" / "elevation" / "lod_neg02"
                       / "n32_w118.tif";
  REQUIRE(fs::exists(lod_neg2));
  GDALDatasetH d2 = GDALOpen(lod_neg2.string().c_str(), GA_ReadOnly);
  REQUIRE(d2 != nullptr);
  CHECK(GDALGetRasterXSize(d2) == 256);
  CHECK(GDALGetRasterYSize(d2) == 256);
  GDALClose(d2);

  CleanDir(root);
}

TEST_CASE("GenerateLodPyramid across multiple geocells",
          "[pyramid][datastore]") {
  auto root = TempDir("pyramid_multi_geocell");

  cdbapi::metadata::DatastoreMetadata meta{
      .id = "test-ds",
      .title = "Multi Geocell Pyramid",
      .description = "2x2 geocells",
      .contact_point = "test@test.com",
      .created = "2026-03-30"};
  auto ds_result = cdbapi::CdbDatastore::Create(
      root, cdbapi::DefaultProfile(), meta);
  REQUIRE(ds_result.has_value());
  auto& ds = ds_result.value();

  // Write 2048x2048 covering 2x2 geocells at LOD 0.
  constexpr int kSize = 2048;
  std::vector<std::byte> pixels(
      static_cast<size_t>(kSize) * kSize * sizeof(float),
      std::byte{0});

  cdbapi::coverage::CoverageDescriptor desc{
      .dataset_name = "elevation",
      .bounds = {32.0, -118.0, 34.0, -116.0},
      .target_lod = 0,
      .metadata = {.field_name = "elevation",
                   .data_null = -32767.0}};
  cdbapi::coverage::CoverageData data{
      .width = kSize, .height = kSize, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  REQUIRE(ds.WriteCoverage(desc, data).has_value());

  // Generate from LOD 0 to LOD -1.
  auto pyramid = ds.GenerateLodPyramid("elevation", 0, -1);
  REQUIRE(pyramid.has_value());

  // All 4 geocells should have LOD -1 tiles.
  auto base = root / "coverages" / "elevation" / "lod_neg01";
  CHECK(fs::exists(base / "n32_w118.tif"));
  CHECK(fs::exists(base / "n32_w117.tif"));
  CHECK(fs::exists(base / "n33_w118.tif"));
  CHECK(fs::exists(base / "n33_w117.tif"));

  // Verify dimensions (LOD -1 = 512x512).
  GDALAllRegister();
  GDALDatasetH d = GDALOpen(
      (base / "n32_w118.tif").string().c_str(), GA_ReadOnly);
  REQUIRE(d != nullptr);
  CHECK(GDALGetRasterXSize(d) == 512);
  CHECK(GDALGetRasterYSize(d) == 512);
  GDALClose(d);

  CleanDir(root);
}

TEST_CASE("GenerateLodPyramid validates inputs",
          "[pyramid][datastore]") {
  auto root = TempDir("pyramid_validation");

  cdbapi::metadata::DatastoreMetadata meta{
      .id = "test-ds",
      .title = "Validation Test",
      .description = "Validation",
      .contact_point = "test@test.com",
      .created = "2026-03-30"};
  auto ds_result = cdbapi::CdbDatastore::Create(
      root, cdbapi::DefaultProfile(), meta);
  REQUIRE(ds_result.has_value());
  auto& ds = ds_result.value();

  SECTION("to_lod must be less than from_lod") {
    CHECK_FALSE(
        ds.GenerateLodPyramid("elevation", 1, 1).has_value());
    CHECK_FALSE(
        ds.GenerateLodPyramid("elevation", 1, 2).has_value());
  }

  SECTION("LOD out of valid range") {
    CHECK_FALSE(
        ds.GenerateLodPyramid("elevation", 99, 0).has_value());
    CHECK_FALSE(
        ds.GenerateLodPyramid("elevation", 1, -99).has_value());
  }

  SECTION("No source tiles at from_lod") {
    CHECK_FALSE(
        ds.GenerateLodPyramid("nonexistent", 1, 0).has_value());
  }

  CleanDir(root);
}
