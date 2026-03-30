#include "cdbapi/format/jpeg2000_writer.h"

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

auto Jpeg2000Writer::WriteTile(
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

  // JP2OpenJPEG is CreateCopy-only — build in MEM first, then copy.
  GDALDriverH mem_driver = GDALGetDriverByName("MEM");
  if (!mem_driver) {
    return std::unexpected(
        Error(ErrorCode::kGdalError, "MEM driver not available"));
  }

  GDALDatasetH mem_ds = GDALCreate(mem_driver, "",
                                   width, height, band_count,
                                   gdal_type, nullptr);
  if (!mem_ds) {
    return std::unexpected(
        Error(ErrorCode::kGdalError, "Failed to create MEM dataset"));
  }

  // Set geotransform.
  double pixel_width = (bounds.east - bounds.west) / width;
  double pixel_height = (bounds.north - bounds.south) / height;
  double gt[6] = {bounds.west, pixel_width, 0.0,
                  bounds.north, 0.0, -pixel_height};
  GDALSetGeoTransform(mem_ds, gt);

  // Set projection.
  std::string wkt = crs.ToWkt2();
  GDALSetProjection(mem_ds, wkt.c_str());

  // Write pixel data per band using BIP stride parameters.
  int pixel_stride = band_count * bps;
  int line_stride = width * band_count * bps;

  for (int b = 0; b < band_count; ++b) {
    GDALRasterBandH band = GDALGetRasterBand(mem_ds, b + 1);
    GDALSetRasterNoDataValue(band, meta.data_null);

    auto* buf = const_cast<void*>(
        static_cast<const void*>(pixels.data() + b * bps));
    CPLErr err = GDALRasterIO(band, GF_Write,
                              0, 0, width, height,
                              buf, width, height,
                              gdal_type,
                              pixel_stride, line_stride);
    if (err != CE_None) {
      GDALClose(mem_ds);
      return std::unexpected(
          Error(ErrorCode::kGdalError,
                std::format("Failed to write band {}", b + 1)));
    }
  }

  // Copy MEM dataset to JP2 file (lossless).
  GDALDriverH jp2_driver = GDALGetDriverByName("JP2OpenJPEG");
  if (!jp2_driver) {
    GDALClose(mem_ds);
    return std::unexpected(
        Error(ErrorCode::kGdalError,
              "JP2OpenJPEG driver not available"));
  }

  char** options = nullptr;
  options = CSLSetNameValue(options, "QUALITY", "100");
  options = CSLSetNameValue(options, "REVERSIBLE", "YES");

  std::string path_str = output_path.string();
  GDALDatasetH jp2_ds = GDALCreateCopy(jp2_driver, path_str.c_str(),
                                       mem_ds, FALSE, options,
                                       nullptr, nullptr);
  CSLDestroy(options);
  GDALClose(mem_ds);

  if (!jp2_ds) {
    return std::unexpected(
        Error(ErrorCode::kGdalError,
              std::format("Failed to create JPEG2000: {}", path_str)));
  }

  GDALClose(jp2_ds);
  return {};
}

}  // namespace cdbapi::format
