#pragma once

#include <utility>
#include <vector>

#include "cdbapi/tiling/tile_address.h"

namespace cdbapi::tiling {

// Geographic bounding box in decimal degrees.
struct GeoBounds {
  double south;
  double west;
  double north;
  double east;
};

// Abstract tiling scheme interface.
class TilingScheme {
 public:
  virtual ~TilingScheme() = default;

  // Returns the supported LOD range [min, max].
  virtual auto LodRange() const -> std::pair<int, int> = 0;

  // Returns the geographic extent of a tile.
  virtual auto TileExtent(const TileAddress& addr) const -> GeoBounds = 0;

  // Returns all tile addresses that intersect the given bounds at the given LOD.
  virtual auto TilesForBounds(const GeoBounds& bounds, int lod) const
      -> std::vector<TileAddress> = 0;

  // Returns the pixel dimensions (width, height) of a tile at the given LOD.
  virtual auto TilePixelSize(int lod) const -> std::pair<int, int> = 0;

  // Returns the coalescence coefficient for a given absolute latitude.
  virtual auto Coalescence(double abs_lat) const -> int = 0;
};

}  // namespace cdbapi::tiling
