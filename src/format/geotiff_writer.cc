#include "cdbapi/format/geotiff_writer.h"

#include <format>

#include "cpl_string.h"
#include "gdal.h"

namespace cdbapi::format {

namespace {

void EnsureGdalRegistered() {
  static bool registered = [] {
    GDALAllRegister();
    return true;
  }();
  (void)registered;
}

auto ToGdalDataType(coverage::PixelType pt) -> GDALDataType {
  switch (pt) {
    case coverage::PixelType::kFloat32: return GDT_Float32;
    case coverage::PixelType::kFloat64: return GDT_Float64;
    case coverage::PixelType::kInt16:   return GDT_Int16;
    case coverage::PixelType::kInt32:   return GDT_Int32;
    case coverage::PixelType::kUint8:   return GDT_Byte;
    case coverage::PixelType::kUint16:  return GDT_UInt16;
  }
  return GDT_Unknown;
}

}  // namespace

auto GeoTiffWriter::WriteTile(
    const std::filesystem::path& output_path,
    std::span<const std::byte> pixels,
    int width, int height, int band_count,
    coverage::PixelType pixel_type,
    const tiling::GeoBounds& bounds,
    const coverage::CoverageMetadata& meta,
    const crs::Crs& crs) -> std::expected<void, Error> {
  int bps = coverage::BytesPerSample(pixel_type);
  auto expected_size =
      static_cast<size_t>(width) * height * band_count * bps;
  if (pixels.size() != expected_size) {
    return std::unexpected(
        Error(ErrorCode::kInvalidArgument,
              std::format("Pixel buffer size {} does not match expected {}",
                          pixels.size(), expected_size)));
  }

  EnsureGdalRegistered();

  GDALDataType gdal_type = ToGdalDataType(pixel_type);
  GDALDriverH driver = GDALGetDriverByName("GTiff");
  if (!driver) {
    return std::unexpected(
        Error(ErrorCode::kGdalError, "GTiff driver not available"));
  }

  char** options = nullptr;
  options = CSLSetNameValue(options, "TILED", "YES");
  options = CSLSetNameValue(options, "BLOCKXSIZE", "256");
  options = CSLSetNameValue(options, "BLOCKYSIZE", "256");

  std::string path_str = output_path.string();
  GDALDatasetH dataset = GDALCreate(driver, path_str.c_str(),
                                    width, height, band_count,
                                    gdal_type, options);
  CSLDestroy(options);

  if (!dataset) {
    return std::unexpected(
        Error(ErrorCode::kGdalError,
              std::format("Failed to create GeoTIFF: {}", path_str)));
  }

  // Set geotransform.
  double pixel_width = (bounds.east - bounds.west) / width;
  double pixel_height = (bounds.north - bounds.south) / height;
  double gt[6] = {bounds.west, pixel_width, 0.0,
                  bounds.north, 0.0, -pixel_height};
  if (GDALSetGeoTransform(dataset, gt) != CE_None) {
    GDALClose(dataset);
    return std::unexpected(
        Error(ErrorCode::kGdalError, "Failed to set geotransform"));
  }

  // Set projection.
  std::string wkt = crs.ToWkt2();
  if (GDALSetProjection(dataset, wkt.c_str()) != CE_None) {
    GDALClose(dataset);
    return std::unexpected(
        Error(ErrorCode::kGdalError, "Failed to set projection"));
  }

  // Write pixel data per band using BIP stride parameters.
  int pixel_stride = band_count * bps;
  int line_stride = width * band_count * bps;

  for (int b = 0; b < band_count; ++b) {
    GDALRasterBandH band = GDALGetRasterBand(dataset, b + 1);

    GDALSetRasterNoDataValue(band, meta.data_null);

    auto* buf = const_cast<void*>(
        static_cast<const void*>(pixels.data() + b * bps));
    CPLErr err = GDALRasterIO(band, GF_Write,
                              0, 0, width, height,
                              buf, width, height,
                              gdal_type,
                              pixel_stride, line_stride);
    if (err != CE_None) {
      GDALClose(dataset);
      return std::unexpected(
          Error(ErrorCode::kGdalError,
                std::format("Failed to write band {}", b + 1)));
    }
  }

  GDALClose(dataset);
  return {};
}

}  // namespace cdbapi::format
