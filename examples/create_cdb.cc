// Example: Read a Copernicus DEM GeoTIFF and write it into a CDB datastore.
//
// Usage: create_cdb <input.tif> <output_cdb_dir>
// Example: create_cdb data/N36_W113_dem.tif my_cdb

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <vector>

#include "cdbapi/cdbapi.h"
#include "cdbapi/format/geotiff_writer.h"

#include "gdal.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::printf("Usage: %s <input.tif> <output_cdb_dir>\n", argv[0]);
    return 1;
  }

  const char* input_path = argv[1];
  const char* output_dir = argv[2];

  // --- Read the source DEM with GDAL ---
  GDALAllRegister();

  std::printf("Opening %s...\n", input_path);
  std::fflush(stdout);

  GDALDatasetH src = GDALOpen(input_path, GA_ReadOnly);
  if (!src) {
    std::printf("ERROR: Cannot open %s\n", input_path);
    return 1;
  }
  std::printf("Opened OK.\n"); std::fflush(stdout);

  int width = GDALGetRasterXSize(src);
  int height = GDALGetRasterYSize(src);
  int bands = GDALGetRasterCount(src);
  std::printf("Size: %d x %d x %d\n", width, height, bands); std::fflush(stdout);

  double gt[6];
  GDALGetGeoTransform(src, gt);
  std::printf("GeoTransform read.\n"); std::fflush(stdout);

  double west = gt[0];
  double north = gt[3];
  double pixel_w = gt[1];
  double pixel_h = gt[5];  // negative
  double east = west + pixel_w * width;
  double south = north + pixel_h * height;
  std::printf("Bounds computed.\n"); std::fflush(stdout);

  GDALRasterBandH band = GDALGetRasterBand(src, 1);
  std::printf("Band: %p\n", (void*)band); std::fflush(stdout);
  GDALDataType src_type = GDALGetRasterDataType(band);
  std::printf("Type: %d\n", (int)src_type); std::fflush(stdout);
  int has_nodata = 0;
  double nodata = GDALGetRasterNoDataValue(band, &has_nodata);
  std::printf("NoData read: has=%d val=%.1f\n", has_nodata, nodata); std::fflush(stdout);

  std::printf("=== Source DEM Info ===\n");
  std::printf("  File:       %s\n", input_path);
  std::printf("  Size:       %d x %d, %d band(s)\n", width, height, bands);
  std::printf("  Type:       %s\n", GDALGetDataTypeName(src_type));
  std::printf("  Bounds:     S=%.6f W=%.6f N=%.6f E=%.6f\n",
              south, west, north, east);
  std::printf("  Pixel size: %.8f x %.8f deg\n", pixel_w, -pixel_h);
  if (has_nodata) {
    std::printf("  NoData:     %.1f\n", nodata);
  }
  std::printf("\n");

  // Map GDAL type to cdbapi PixelType.
  cdbapi::coverage::PixelType pixel_type;
  int bps;
  switch (src_type) {
    case GDT_Float32: pixel_type = cdbapi::coverage::PixelType::kFloat32; bps = 4; break;
    case GDT_Float64: pixel_type = cdbapi::coverage::PixelType::kFloat64; bps = 8; break;
    case GDT_Int16:   pixel_type = cdbapi::coverage::PixelType::kInt16;   bps = 2; break;
    case GDT_Int32:   pixel_type = cdbapi::coverage::PixelType::kInt32;   bps = 4; break;
    case GDT_Byte:    pixel_type = cdbapi::coverage::PixelType::kUint8;   bps = 1; break;
    case GDT_UInt16:  pixel_type = cdbapi::coverage::PixelType::kUint16;  bps = 2; break;
    default:
      std::printf("ERROR: Unsupported pixel type: %s\n",
                  GDALGetDataTypeName(src_type));
      GDALClose(src);
      return 1;
  }

  std::printf("About to read %zu bytes of pixels...\n",
              static_cast<size_t>(width) * height * bands * bps);
  std::fflush(stdout);

  // Read all pixels.
  std::vector<std::byte> pixels(
      static_cast<size_t>(width) * height * bands * bps);
  std::printf("Allocated buffer. Reading raster...\n"); std::fflush(stdout);

  CPLErr err = GDALRasterIO(band, GF_Read,
                             0, 0, width, height,
                             static_cast<void*>(pixels.data()),
                             width, height,
                             src_type, 0, 0);
  std::printf("Read result: %d\n", (int)err); std::fflush(stdout);
  GDALClose(src);
  std::printf("Source closed.\n"); std::fflush(stdout);

  if (err != CE_None) {
    std::printf("ERROR: Failed to read raster data\n");
    return 1;
  }

  std::printf("Read %zu bytes of pixel data.\n", pixels.size());
  std::fflush(stdout);

  // Snap bounds to nearest geocell boundaries.
  // Copernicus DEM has half-pixel offsets, round to integer degrees.
  south = std::round(south);
  west = std::round(west);
  north = std::round(north);
  east = std::round(east);
  std::printf("Snapped bounds: S=%.0f W=%.0f N=%.0f E=%.0f\n",
              south, west, north, east);
  std::fflush(stdout);

  // --- Create CDB datastore ---
  cdbapi::metadata::DatastoreMetadata meta{
      .id = "grand-canyon-demo",
      .title = "Grand Canyon CDB Demo",
      .description = "CDB 2.0 datastore from Copernicus DEM 30m",
      .contact_point = "cdbapi++ user",
      .created = "2026-03-28"};

  auto ds_result = cdbapi::CdbDatastore::Create(
      output_dir, cdbapi::DefaultProfile(), meta);
  if (!ds_result) {
    std::printf("ERROR: %s\n",
                std::string(ds_result.error().message()).c_str());
    return 1;
  }
  auto& ds = ds_result.value();
  std::printf("Created CDB datastore at: %s\n", output_dir);

  // --- Write the coverage ---
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
      .width = width,
      .height = height,
      .band_count = bands,
      .pixel_type = pixel_type,
      .pixels = std::move(pixels)};

  std::printf("Writing coverage '%s' at LOD 0 (%dx%d pixels)...\n",
              desc.dataset_name.c_str(), cov_data.width, cov_data.height);
  std::fflush(stdout);

  auto write_result = ds.WriteCoverage(desc, cov_data);
  std::printf("WriteCoverage returned.\n"); std::fflush(stdout);
  if (!write_result) {
    std::printf("ERROR: %s\n",
                std::string(write_result.error().message()).c_str());
    return 1;
  }

  // --- Finalize ---
  auto fin = ds.Finalize();
  if (!fin) {
    std::printf("ERROR: %s\n",
                std::string(fin.error().message()).c_str());
    return 1;
  }

  // --- Report what was created ---
  std::printf("\n=== CDB Datastore Created ===\n");
  for (auto& entry : fs::recursive_directory_iterator(output_dir)) {
    if (entry.is_regular_file()) {
      auto rel = fs::relative(entry.path(), output_dir).string();
      std::replace(rel.begin(), rel.end(), '\\', '/');
      std::printf("  %s  (%s)\n", rel.c_str(),
                  entry.file_size() > 1024 * 1024
                      ? (std::to_string(entry.file_size() / (1024 * 1024)) + " MB").c_str()
                      : (std::to_string(entry.file_size()) + " bytes").c_str());
    }
  }

  std::printf("\nDone!\n");
  return 0;
}
