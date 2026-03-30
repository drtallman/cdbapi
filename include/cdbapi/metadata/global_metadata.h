#pragma once

#include <expected>
#include <filesystem>
#include <string>

#include "cdbapi/application_profile.h"
#include "cdbapi/error.h"

namespace cdbapi::metadata {

// Global datastore metadata (DCAT-aligned mandatory fields).
struct DatastoreMetadata {
  std::string id;              // Persistent unique identifier
  std::string title;           // Short human-readable title
  std::string description;     // Detailed description
  std::string contact_point;   // Contact entity
  std::string created;         // Creation date (RFC 3339)
  std::string language = "en"; // IETF BCP 47 language tag
};

// Writes all global metadata files to the global_metadata directory:
//   - version.xml
//   - crs.wkt
//   - datastore_metadata.xml (or .json depending on profile)
auto WriteGlobalMetadata(const std::filesystem::path& metadata_dir,
                         const DatastoreMetadata& meta,
                         const ApplicationProfile& profile)
    -> std::expected<void, Error>;

// Reads datastore_metadata.xml from the global_metadata directory.
auto ReadDatastoreMetadata(const std::filesystem::path& metadata_dir)
    -> std::expected<DatastoreMetadata, Error>;

// Reads the CRS EPSG code from crs.wkt in the global_metadata directory.
auto ReadCrsEpsg(const std::filesystem::path& metadata_dir)
    -> std::expected<int, Error>;

}  // namespace cdbapi::metadata
