#include "cdbapi/path/path_builder.h"

#include <format>

#include "cdbapi/tiling/geocell.h"
#include "cdbapi/tiling/tile_address.h"

namespace cdbapi::path {

PathBuilder::PathBuilder(std::filesystem::path root,
                         NamingConvention convention)
    : root_(std::move(root)), convention_(convention) {}

auto PathBuilder::GlobalMetadataDir() const -> std::filesystem::path {
  return root_ / "global_metadata";
}

auto PathBuilder::CoverageLodDir(std::string_view dataset_name,
                                 int lod) const -> std::filesystem::path {
  return root_ / "coverages" / std::string(dataset_name) /
         tiling::FormatLod(lod);
}

auto PathBuilder::CoveragePath(std::string_view dataset_name,
                               const tiling::TileAddress& addr,
                               std::string_view extension)
    const -> std::filesystem::path {
  auto dir = CoverageLodDir(dataset_name, addr.lod);
  std::string filename = tiling::FormatTileAddress(addr) +
                         "." + std::string(extension);
  return dir / filename;
}

}  // namespace cdbapi::path
