#pragma once

#include <expected>
#include <filesystem>
#include <string>

#include "cdbapi/error.h"

namespace cdbapi::metadata {

struct VersionInfo {
  std::string specification = "OGC CDB";
  std::string specification_version = "2.0";
  int datastore_version = 1;
};

// Writes version.xml to the given directory.
auto WriteVersionXml(const std::filesystem::path& metadata_dir,
                     const VersionInfo& info) -> std::expected<void, Error>;

}  // namespace cdbapi::metadata
