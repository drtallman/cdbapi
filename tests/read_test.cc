#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstring>
#include <filesystem>
#include <vector>

#include "cdbapi/cdbapi.h"

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

// Helper: creates a datastore, writes a coverage, finalizes, returns root.
auto CreateTestDatastore(const fs::path& root) {
  cdbapi::metadata::DatastoreMetadata meta{
      .id = "roundtrip-test",
      .title = "Round-trip Test",
      .description = "Read/write round-trip",
      .contact_point = "test@test.com",
      .created = "2026-03-30"};
  return cdbapi::CdbDatastore::Create(
      root, cdbapi::DefaultProfile(), meta);
}

}  // namespace

TEST_CASE("Open reads back datastore metadata", "[read][open]") {
  auto root = TempDir("read_open");

  // Write a datastore.
  auto create_result = CreateTestDatastore(root);
  REQUIRE(create_result.has_value());
  auto& ds_write = create_result.value();

  constexpr int kSize = 1024;
  std::vector<std::byte> pixels(
      static_cast<size_t>(kSize) * kSize * sizeof(float), std::byte{0});

  cdbapi::coverage::CoverageDescriptor desc{
      .dataset_name = "elevation",
      .bounds = {32.0, -118.0, 33.0, -117.0},
      .target_lod = 0,
      .metadata = {.field_name = "elevation", .data_null = -32767.0}};
  cdbapi::coverage::CoverageData data{
      .width = kSize, .height = kSize, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  REQUIRE(ds_write.WriteCoverage(desc, data).has_value());
  REQUIRE(ds_write.Finalize().has_value());

  // Re-open the datastore.
  auto open_result = cdbapi::CdbDatastore::Open(root);
  REQUIRE(open_result.has_value());
  auto& ds_read = open_result.value();

  CHECK(ds_read.profile().epsg_code == 4326);
  CHECK(ds_read.root() == root);

  CleanDir(root);
}

TEST_CASE("ListDatasets returns written datasets", "[read][datasets]") {
  auto root = TempDir("read_list_ds");

  auto create_result = CreateTestDatastore(root);
  REQUIRE(create_result.has_value());
  auto& ds = create_result.value();

  constexpr int kSize = 1024;
  std::vector<std::byte> pixels(
      static_cast<size_t>(kSize) * kSize * sizeof(float), std::byte{0});

  // Write two datasets.
  cdbapi::coverage::CoverageDescriptor desc1{
      .dataset_name = "elevation",
      .bounds = {32.0, -118.0, 33.0, -117.0},
      .target_lod = 0,
      .metadata = {.field_name = "elevation", .data_null = -32767.0}};
  cdbapi::coverage::CoverageData data{
      .width = kSize, .height = kSize, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  REQUIRE(ds.WriteCoverage(desc1, data).has_value());

  cdbapi::coverage::CoverageDescriptor desc2{
      .dataset_name = "terrain",
      .bounds = {32.0, -118.0, 33.0, -117.0},
      .target_lod = 0,
      .metadata = {.field_name = "terrain", .data_null = -32767.0}};
  REQUIRE(ds.WriteCoverage(desc2, data).has_value());
  REQUIRE(ds.Finalize().has_value());

  // Re-open and list.
  auto open_result = cdbapi::CdbDatastore::Open(root);
  REQUIRE(open_result.has_value());
  auto& ds_read = open_result.value();

  auto& datasets = ds_read.ListDatasets();
  REQUIRE(datasets.size() == 2);

  // Order may vary, check both exist.
  bool has_elevation = false, has_terrain = false;
  for (const auto& d : datasets) {
    if (d.id == "elevation") has_elevation = true;
    if (d.id == "terrain") has_terrain = true;
  }
  CHECK(has_elevation);
  CHECK(has_terrain);

  CleanDir(root);
}

TEST_CASE("LodRange returns correct range", "[read][lodrange]") {
  auto root = TempDir("read_lodrange");

  auto create_result = CreateTestDatastore(root);
  REQUIRE(create_result.has_value());
  auto& ds = create_result.value();

  // Write at LOD 0 and generate pyramid to LOD -2.
  constexpr int kSize = 1024;
  std::vector<std::byte> pixels(
      static_cast<size_t>(kSize) * kSize * sizeof(float), std::byte{0});

  cdbapi::coverage::CoverageDescriptor desc{
      .dataset_name = "elevation",
      .bounds = {32.0, -118.0, 33.0, -117.0},
      .target_lod = 0,
      .metadata = {.field_name = "elevation", .data_null = -32767.0}};
  cdbapi::coverage::CoverageData data{
      .width = kSize, .height = kSize, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  REQUIRE(ds.WriteCoverage(desc, data).has_value());
  REQUIRE(ds.GenerateLodPyramid("elevation", 0, -2).has_value());
  REQUIRE(ds.Finalize().has_value());

  // Re-open and check LOD range.
  auto open_result = cdbapi::CdbDatastore::Open(root);
  REQUIRE(open_result.has_value());
  auto& ds_read = open_result.value();

  auto range = ds_read.LodRange("elevation");
  REQUIRE(range.has_value());
  CHECK(range->first == -2);
  CHECK(range->second == 0);

  // Non-existent dataset.
  CHECK_FALSE(ds_read.LodRange("nonexistent").has_value());

  CleanDir(root);
}

TEST_CASE("ReadCoverage round-trips pixel data", "[read][coverage]") {
  auto root = TempDir("read_roundtrip");

  auto create_result = CreateTestDatastore(root);
  REQUIRE(create_result.has_value());
  auto& ds = create_result.value();

  // Write a tile with known values.
  constexpr int kSize = 1024;
  std::vector<float> values(kSize * kSize);
  for (int i = 0; i < kSize * kSize; ++i) {
    values[i] = static_cast<float>(i) * 0.1f;
  }
  std::vector<std::byte> pixels(values.size() * sizeof(float));
  std::memcpy(pixels.data(), values.data(), pixels.size());

  cdbapi::coverage::CoverageDescriptor desc{
      .dataset_name = "elevation",
      .bounds = {32.0, -118.0, 33.0, -117.0},
      .target_lod = 0,
      .metadata = {.field_name = "elevation", .data_null = -32767.0}};
  cdbapi::coverage::CoverageData data{
      .width = kSize, .height = kSize, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  REQUIRE(ds.WriteCoverage(desc, data).has_value());
  REQUIRE(ds.Finalize().has_value());

  // Re-open and read the tile.
  auto open_result = cdbapi::CdbDatastore::Open(root);
  REQUIRE(open_result.has_value());
  auto& ds_read = open_result.value();

  cdbapi::tiling::TileAddress addr{0, {32, -118}, 0, 0};
  auto read_result = ds_read.ReadCoverage("elevation", addr);
  REQUIRE(read_result.has_value());

  auto& cov = read_result.value();
  CHECK(cov.width == kSize);
  CHECK(cov.height == kSize);
  CHECK(cov.band_count == 1);
  CHECK(cov.pixel_type == cdbapi::coverage::PixelType::kFloat32);
  REQUIRE(cov.pixels.size() == pixels.size());

  // Verify pixel values match.
  const float* read_vals = reinterpret_cast<const float*>(cov.pixels.data());
  CHECK(read_vals[0] == Catch::Approx(0.0f));
  CHECK(read_vals[1] == Catch::Approx(0.1f));
  CHECK(read_vals[1023] == Catch::Approx(102.3f));
  CHECK(read_vals[kSize * kSize - 1] ==
        Catch::Approx(static_cast<float>(kSize * kSize - 1) * 0.1f));

  CleanDir(root);
}

TEST_CASE("ReadCoverage returns error for missing tile", "[read][coverage]") {
  auto root = TempDir("read_missing");

  auto create_result = CreateTestDatastore(root);
  REQUIRE(create_result.has_value());
  auto& ds = create_result.value();
  REQUIRE(ds.Finalize().has_value());

  auto open_result = cdbapi::CdbDatastore::Open(root);
  REQUIRE(open_result.has_value());
  auto& ds_read = open_result.value();

  cdbapi::tiling::TileAddress addr{0, {99, 99}, 0, 0};
  CHECK_FALSE(ds_read.ReadCoverage("elevation", addr).has_value());

  CleanDir(root);
}

TEST_CASE("Open rejects invalid paths", "[read][open]") {
  CHECK_FALSE(cdbapi::CdbDatastore::Open("").has_value());
  CHECK_FALSE(
      cdbapi::CdbDatastore::Open("nonexistent_dir_xyz").has_value());
}
