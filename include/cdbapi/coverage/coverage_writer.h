#pragma once

#include <expected>
#include <filesystem>
#include <span>

#include "cdbapi/coverage/coverage.h"
#include "cdbapi/coverage/coverage_metadata.h"
#include "cdbapi/crs/crs.h"
#include "cdbapi/error.h"
#include "cdbapi/tiling/tiling_scheme.h"

namespace cdbapi::coverage {

// Abstract interface for writing a single coverage tile to disk.
class CoverageWriter {
 public:
  virtual ~CoverageWriter() = default;

  virtual auto WriteTile(
      const std::filesystem::path& output_path,
      std::span<const std::byte> pixels,
      int width, int height, int band_count,
      PixelType pixel_type,
      const tiling::GeoBounds& bounds,
      const CoverageMetadata& meta,
      const crs::Crs& crs) -> std::expected<void, Error> = 0;
};

}  // namespace cdbapi::coverage
