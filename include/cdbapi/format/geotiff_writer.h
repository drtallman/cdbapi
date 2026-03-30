#pragma once

#include "cdbapi/coverage/coverage_writer.h"

namespace cdbapi::format {

class GeoTiffWriter : public coverage::CoverageWriter {
 public:
  auto WriteTile(
      const std::filesystem::path& output_path,
      std::span<const std::byte> pixels,
      int width, int height, int band_count,
      coverage::PixelType pixel_type,
      const tiling::GeoBounds& bounds,
      const coverage::CoverageMetadata& meta,
      const crs::Crs& crs) -> std::expected<void, Error> override;
};

}  // namespace cdbapi::format
