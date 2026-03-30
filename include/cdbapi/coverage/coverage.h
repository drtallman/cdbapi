#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "cdbapi/coverage/coverage_metadata.h"
#include "cdbapi/tiling/tiling_scheme.h"

namespace cdbapi::coverage {

enum class PixelType {
  kFloat32,
  kFloat64,
  kInt16,
  kInt32,
  kUint8,
  kUint16,
};

// Returns the number of bytes per sample for a given pixel type.
constexpr auto BytesPerSample(PixelType pt) -> int {
  switch (pt) {
    case PixelType::kFloat32: return 4;
    case PixelType::kFloat64: return 8;
    case PixelType::kInt16:   return 2;
    case PixelType::kInt32:   return 4;
    case PixelType::kUint8:   return 1;
    case PixelType::kUint16:  return 2;
  }
  return 0;
}

// Holds raw pixel data for a coverage region.
struct CoverageData {
  int width;
  int height;
  int band_count = 1;
  PixelType pixel_type = PixelType::kFloat32;
  std::vector<std::byte> pixels;  // Row-major, band-interleaved-by-pixel
};

// Describes a coverage to be written to the datastore.
struct CoverageDescriptor {
  std::string dataset_name;                  // e.g., "elevation"
  tiling::GeoBounds bounds;                  // Geographic extent of the data
  int target_lod;                            // LOD to write at
  CoverageMetadata metadata;
};

}  // namespace cdbapi::coverage
