#pragma once

#include <filesystem>
#include <string_view>

#include "cdbapi/application_profile.h"
#include "cdbapi/tiling/tile_address.h"

namespace cdbapi::path {

// Generates spec-compliant filesystem paths for CDB datastore assets.
class PathBuilder {
 public:
  explicit PathBuilder(std::filesystem::path root,
                       NamingConvention convention = NamingConvention::kSnakeCase);

  // Returns the root path of the datastore.
  auto Root() const -> const std::filesystem::path& { return root_; }

  // Returns path to global_metadata directory.
  auto GlobalMetadataDir() const -> std::filesystem::path;

  // Returns path to a coverage tile file.
  // E.g., root/coverages/elevation/lod_00/n32_w118.tif
  auto CoveragePath(std::string_view dataset_name,
                    const tiling::TileAddress& addr,
                    std::string_view extension) const -> std::filesystem::path;

  // Returns path to a coverage LOD directory.
  // E.g., root/coverages/elevation/lod_02
  auto CoverageLodDir(std::string_view dataset_name,
                      int lod) const -> std::filesystem::path;

 private:
  std::filesystem::path root_;
  NamingConvention convention_;
};

}  // namespace cdbapi::path
