#pragma once

#include <optional>
#include <string>

namespace cdbapi {

enum class TilingSchemeType {
  kCdb1GlobalGrid,
  kGnosisGlobalGrid,
};

enum class NamingConvention {
  kSnakeCase,
  kCamelCase,
  kPascalCase,
  kKebabCase,
};

enum class MetadataEncoding {
  kXml,
  kJson,
  kGeoPackage,
};

enum class CoverageEncoding {
  kGeoTiff,
  kGeoPackage,
  kJpeg2000,
  kPng,
  kTiff,
  kNetCdf,
  kGml,
  kCoverageJson,
};

enum class UnitOfMeasure {
  kMeters,
  kFeet,
  kKilometers,
  kMiles,
};

struct ApplicationProfile {
  std::string name = "cdbapi_default_v1";
  int epsg_code = 4326;
  std::optional<int> vertical_epsg_code = std::nullopt;
  TilingSchemeType tiling = TilingSchemeType::kCdb1GlobalGrid;
  NamingConvention naming = NamingConvention::kSnakeCase;
  MetadataEncoding metadata_encoding = MetadataEncoding::kXml;
  CoverageEncoding default_coverage_encoding = CoverageEncoding::kGeoTiff;
  std::string language = "en";
  std::optional<double> epoch = std::nullopt;
  UnitOfMeasure uom = UnitOfMeasure::kMeters;
};

// Returns the default application profile: EPSG:4326, CDB1GlobalGrid,
// snake_case, XML metadata, GeoTIFF coverages, meters.
auto DefaultProfile() -> ApplicationProfile;

}  // namespace cdbapi
