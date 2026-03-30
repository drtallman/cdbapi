#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cdbapi/application_profile.h"
#include "cdbapi/coverage/coverage.h"
#include "cdbapi/error.h"
#include "cdbapi/metadata/dataset_metadata.h"
#include "cdbapi/metadata/global_metadata.h"
#include "cdbapi/tiling/tile_address.h"

namespace cdbapi {

// Top-level handle for a CDB Version 2 datastore.
// Use Create() to make a new datastore, or Open() to read an existing one.
class CdbDatastore {
 public:
  ~CdbDatastore();
  CdbDatastore(CdbDatastore&&) noexcept;
  auto operator=(CdbDatastore&&) noexcept -> CdbDatastore&;

  // Creates a new datastore at the given root path.
  static auto Create(const std::filesystem::path& root,
                     const ApplicationProfile& profile,
                     const metadata::DatastoreMetadata& meta)
      -> std::expected<CdbDatastore, Error>;

  // Opens an existing datastore for reading (and optional further writing).
  // Parses global metadata to recover profile and dataset list.
  static auto Open(const std::filesystem::path& root)
      -> std::expected<CdbDatastore, Error>;

  auto root() const -> const std::filesystem::path&;
  auto profile() const -> const ApplicationProfile&;

  // Returns the list of datasets in the datastore.
  auto ListDatasets() const -> const std::vector<metadata::DatasetInfo>&;

  // Returns the range of LODs [min, max] that exist for a dataset,
  // or an error if the dataset is not found.
  auto LodRange(const std::string& dataset) const
      -> std::expected<std::pair<int, int>, Error>;

  // Reads coverage data for a single tile.
  auto ReadCoverage(const std::string& dataset,
                    const tiling::TileAddress& addr)
      -> std::expected<coverage::CoverageData, Error>;

  // Writes coverage data into the datastore, tiling it as needed.
  auto WriteCoverage(const coverage::CoverageDescriptor& desc,
                     const coverage::CoverageData& data)
      -> std::expected<void, Error>;

  // Generates LOD pyramid by downsampling from from_lod to to_lod.
  // Reads tiles at from_lod and produces progressively coarser tiles
  // down to to_lod (exclusive of from_lod, inclusive of to_lod).
  // Uses box-filter (average) resampling with nodata-aware averaging.
  auto GenerateLodPyramid(const std::string& dataset,
                          int from_lod, int to_lod)
      -> std::expected<void, Error>;

  // Flushes metadata and finalizes the datastore.
  auto Finalize() -> std::expected<void, Error>;

 private:
  CdbDatastore();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cdbapi
