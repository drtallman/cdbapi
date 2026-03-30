// Integration test: full round-trip write/read of a Copernicus DEM tile.
//
// Usage: roundtrip <input.tif>
// Example: roundtrip data/N36_W113_dem.tif
//
// This program:
//   1. Reads a source GeoTIFF DEM with GDAL
//   2. Creates a CDB datastore and writes the coverage at LOD 0
//   3. Generates an LOD pyramid from LOD 0 down to LOD -3
//   4. Finalizes the datastore
//   5. Re-opens the datastore via CdbDatastore::Open()
//   6. Lists datasets and LOD range
//   7. Reads tiles back and compares pixel values to the original

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "cdbapi/cdbapi.h"

#include "gdal.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::printf("Usage: %s <input.tif>\n", argv[0]);
    return 1;
  }

  const char* input_path = argv[1];
  auto output_dir = fs::path(argv[1]).parent_path().parent_path()
                    / "roundtrip_cdb";

  // Clean any prior run.
  fs::remove_all(output_dir);

  // --- Step 1: Read the source DEM ---
  GDALAllRegister();
  std::printf("1. Reading source DEM: %s\n", input_path);

  GDALDatasetH src = GDALOpen(input_path, GA_ReadOnly);
  if (!src) {
    std::printf("   ERROR: Cannot open %s\n", input_path);
    return 1;
  }

  int width = GDALGetRasterXSize(src);
  int height = GDALGetRasterYSize(src);
  std::printf("   Size: %d x %d\n", width, height);

  double gt[6];
  GDALGetGeoTransform(src, gt);
  double west = gt[0], north = gt[3];
  double east = west + gt[1] * width;
  double south = north + gt[5] * height;

  // Snap to integer degrees.
  south = std::round(south);
  west = std::round(west);
  north = std::round(north);
  east = std::round(east);
  std::printf("   Bounds: S=%.0f W=%.0f N=%.0f E=%.0f\n",
              south, west, north, east);

  GDALRasterBandH band = GDALGetRasterBand(src, 1);
  int has_nodata = 0;
  double nodata = GDALGetRasterNoDataValue(band, &has_nodata);

  // Read at LOD 0 tile resolution (1024x1024) — GDAL handles resampling.
  constexpr int kTileSize = 1024;
  std::vector<float> original(static_cast<size_t>(kTileSize) * kTileSize);
  GDALRasterIO(band, GF_Read, 0, 0, width, height,
               original.data(), kTileSize, kTileSize,
               GDT_Float32, 0, 0);
  GDALClose(src);
  width = kTileSize;
  height = kTileSize;

  std::printf("   Resampled to %dx%d for LOD 0. First=%.2f, nodata=%.0f\n",
              width, height, original[0], nodata);

  // --- Step 2: Create CDB and write coverage ---
  std::printf("\n2. Creating CDB datastore at: %s\n",
              output_dir.string().c_str());

  cdbapi::metadata::DatastoreMetadata meta{
      .id = "roundtrip-integration-test",
      .title = "Round-trip Integration Test",
      .description = "Verifies write/read fidelity with Copernicus DEM",
      .contact_point = "cdbapi++ test",
      .created = "2026-03-30"};

  auto ds_result = cdbapi::CdbDatastore::Create(
      output_dir, cdbapi::DefaultProfile(), meta);
  if (!ds_result) {
    std::printf("   ERROR: %s\n",
                std::string(ds_result.error().message()).c_str());
    return 1;
  }
  auto& ds = ds_result.value();

  // Convert to byte buffer.
  std::vector<std::byte> pixels(original.size() * sizeof(float));
  std::memcpy(pixels.data(), original.data(), pixels.size());

  cdbapi::coverage::CoverageDescriptor desc{
      .dataset_name = "elevation",
      .bounds = {south, west, north, east},
      .target_lod = 0,
      .metadata = {
          .field_name = "elevation",
          .quantity_definition = "height above EGM2008 geoid",
          .uom = "m",
          .data_null = has_nodata ? nodata : -32767.0}};
  cdbapi::coverage::CoverageData cov_data{
      .width = width, .height = height, .band_count = 1,
      .pixel_type = cdbapi::coverage::PixelType::kFloat32,
      .pixels = pixels};

  auto write_result = ds.WriteCoverage(desc, cov_data);
  if (!write_result) {
    std::printf("   ERROR: %s\n",
                std::string(write_result.error().message()).c_str());
    return 1;
  }
  std::printf("   Wrote coverage at LOD 0.\n");

  // --- Step 3: Generate LOD pyramid ---
  std::printf("\n3. Generating LOD pyramid from LOD 0 to LOD -3...\n");
  auto pyramid = ds.GenerateLodPyramid("elevation", 0, -3);
  if (!pyramid) {
    std::printf("   ERROR: %s\n",
                std::string(pyramid.error().message()).c_str());
    return 1;
  }
  std::printf("   Pyramid generated.\n");

  // --- Step 4: Finalize ---
  std::printf("\n4. Finalizing...\n");
  auto fin = ds.Finalize();
  if (!fin) {
    std::printf("   ERROR: %s\n",
                std::string(fin.error().message()).c_str());
    return 1;
  }

  // List what was created.
  int file_count = 0;
  for (auto& entry : fs::recursive_directory_iterator(output_dir)) {
    if (entry.is_regular_file()) ++file_count;
  }
  std::printf("   Datastore contains %d files.\n", file_count);

  // --- Step 5: Re-open ---
  std::printf("\n5. Re-opening datastore...\n");
  auto open_result = cdbapi::CdbDatastore::Open(output_dir);
  if (!open_result) {
    std::printf("   ERROR: %s\n",
                std::string(open_result.error().message()).c_str());
    return 1;
  }
  auto& ds_read = open_result.value();

  std::printf("   EPSG: %d\n", ds_read.profile().epsg_code);

  // --- Step 6: List datasets and LOD range ---
  std::printf("\n6. Datasets:\n");
  auto& datasets = ds_read.ListDatasets();
  for (const auto& d : datasets) {
    std::printf("   - %s: %s\n", d.id.c_str(), d.description.c_str());
  }

  auto lod_range = ds_read.LodRange("elevation");
  if (!lod_range) {
    std::printf("   ERROR: %s\n",
                std::string(lod_range.error().message()).c_str());
    return 1;
  }
  std::printf("   LOD range: [%d, %d]\n",
              lod_range->first, lod_range->second);

  // --- Step 7: Read LOD 0 tile back and compare ---
  std::printf("\n7. Reading LOD 0 tile and comparing...\n");

  int geocell_lat = static_cast<int>(south);
  int geocell_lon = static_cast<int>(west);
  cdbapi::tiling::TileAddress addr{0, {geocell_lat, geocell_lon}, 0, 0};

  auto read_result = ds_read.ReadCoverage("elevation", addr);
  if (!read_result) {
    std::printf("   ERROR: %s\n",
                std::string(read_result.error().message()).c_str());
    return 1;
  }

  auto& read_cov = read_result.value();
  std::printf("   Read tile: %d x %d, %d band(s)\n",
              read_cov.width, read_cov.height, read_cov.band_count);

  // Compare pixel values.
  // The written tile may have been resampled to 1024x1024 if the source
  // wasn't exactly that size, so we compare the first few pixels at the
  // resolution that was written.
  const float* read_pixels =
      reinterpret_cast<const float*>(read_cov.pixels.data());

  int check_count = 0;
  int match_count = 0;
  int nodata_count = 0;
  double max_diff = 0.0;

  int cmp_size = std::min(width * height,
                          read_cov.width * read_cov.height);
  for (int i = 0; i < cmp_size; ++i) {
    float orig = original[i];
    float read = read_pixels[i];

    if (orig == static_cast<float>(nodata)) {
      ++nodata_count;
      continue;
    }
    ++check_count;
    double diff = std::abs(static_cast<double>(orig) - read);
    max_diff = std::max(max_diff, diff);
    if (diff < 0.01) ++match_count;
  }

  std::printf("   Compared %d non-nodata pixels (%d nodata skipped)\n",
              check_count, nodata_count);
  std::printf("   Matches (within 0.01): %d / %d (%.1f%%)\n",
              match_count, check_count,
              check_count > 0
                  ? 100.0 * match_count / check_count : 0.0);
  std::printf("   Max difference: %.6f\n", max_diff);

  bool pass = (check_count > 0 && match_count == check_count);
  std::printf("\n%s: Round-trip %s!\n",
              pass ? "PASS" : "FAIL",
              pass ? "pixel-perfect" : "has discrepancies");

  // Keep on disk for inspection.
  std::printf("\nDatastore saved at: %s\n", output_dir.string().c_str());

  return pass ? 0 : 1;
}
