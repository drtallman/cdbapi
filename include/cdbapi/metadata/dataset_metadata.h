#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "cdbapi/error.h"
#include "cdbapi/tiling/tiling_scheme.h"

namespace cdbapi::metadata {

struct DatasetInfo {
  std::string id;
  std::string title;
  std::string description;
  std::optional<std::string> publisher;
  std::optional<std::string> created;
  std::optional<std::string> updated;
  std::optional<tiling::GeoBounds> extent;
  std::vector<std::string> keywords;
};

// Writes a datasets metadata file listing all datasets in the datastore.
auto WriteDatasetsMetadata(const std::filesystem::path& metadata_dir,
                           const std::vector<DatasetInfo>& datasets)
    -> std::expected<void, Error>;

// Reads datasets.xml from the global_metadata directory.
auto ReadDatasetsMetadata(const std::filesystem::path& metadata_dir)
    -> std::expected<std::vector<DatasetInfo>, Error>;

}  // namespace cdbapi::metadata
