#pragma once

#include <string>

namespace cdbapi::coverage {

enum class GridCellEncoding {
  kValueIsCenter,
  kValueIsArea,
  kValueIsCornerLowerLeft,
  kValueIsCornerUpperLeft,
  kValueIsCornerLowerRight,
  kValueIsCornerUpperRight,
};

struct CoverageMetadata {
  std::string field_name;             // e.g., "elevation"
  std::string quantity_definition;    // e.g., "height above WGS84 ellipsoid"
  std::string uom = "m";             // Unit of measure for pixel values
  double precision = 1.0;            // Smallest meaningful value
  double scale = 1.0;                // Multiple relative to UoM
  double offset = 0.0;               // Offset to zero
  double data_null = -32767.0;       // NULL indicator value
  GridCellEncoding grid_cell_encoding = GridCellEncoding::kValueIsCenter;
};

}  // namespace cdbapi::coverage
