#pragma once

#include "cdbapi/tiling/tiling_scheme.h"

namespace cdbapi::tiling {

// CDB 1.x backward-compatible global tiling grid.
// Uses EPSG:4326 (lat/lon in decimal degrees).
// LOD 0: 1-degree x 1-degree geocells, 360x180 matrix.
// LOD -10 to 0: one tile per geocell, pixel size halves per negative LOD.
// LOD 1+: quad-tree subdivision, 2^lod x 2^lod tiles per geocell, 1024x1024.
// Variable-width tiles at high latitudes via coalescence zones.
class Cdb1GlobalGrid : public TilingScheme {
 public:
  static constexpr int kMinLod = -10;
  static constexpr int kMaxLod = 23;
  static constexpr int kTilePixels = 1024;

  auto LodRange() const -> std::pair<int, int> override;
  auto TileExtent(const TileAddress& addr) const -> GeoBounds override;
  auto TilesForBounds(const GeoBounds& bounds, int lod) const
      -> std::vector<TileAddress> override;
  auto TilePixelSize(int lod) const -> std::pair<int, int> override;
  auto Coalescence(double abs_lat) const -> int override;
};

}  // namespace cdbapi::tiling
