#include "cdbapi/tiling/cdb1_global_grid.h"

#include <algorithm>
#include <cmath>

#include "cdbapi/tiling/coalescence.h"

namespace cdbapi::tiling {

auto Cdb1GlobalGrid::LodRange() const -> std::pair<int, int> {
  return {kMinLod, kMaxLod};
}

auto Cdb1GlobalGrid::TilePixelSize(int lod) const -> std::pair<int, int> {
  if (lod >= 0) {
    return {kTilePixels, kTilePixels};
  }
  int dim = kTilePixels >> (-lod);
  return {std::max(dim, 1), std::max(dim, 1)};
}

auto Cdb1GlobalGrid::Coalescence(double abs_lat) const -> int {
  return CoalescenceForLatitude(abs_lat);
}

auto Cdb1GlobalGrid::TileExtent(const TileAddress& addr) const -> GeoBounds {
  double abs_lat = std::abs(static_cast<double>(addr.geocell.lat));
  int coal = CoalescenceForLatitude(abs_lat);

  if (addr.lod <= 0) {
    // One tile per geocell. Width is coal degrees in longitude.
    double south = static_cast<double>(addr.geocell.lat);
    double west = static_cast<double>(addr.geocell.lon);
    return GeoBounds{south, west, south + 1.0, west + static_cast<double>(coal)};
  }

  // LOD >= 1: quad-tree subdivision of the geocell.
  int tiles_per_side = 1 << addr.lod;  // 2^lod
  double cell_lat_extent = 1.0;
  double cell_lon_extent = static_cast<double>(coal);

  double tile_lat_size = cell_lat_extent / tiles_per_side;
  double tile_lon_size = cell_lon_extent / tiles_per_side;

  double south = static_cast<double>(addr.geocell.lat) +
                 addr.tile_row * tile_lat_size;
  double west = static_cast<double>(addr.geocell.lon) +
                addr.tile_col * tile_lon_size;

  return GeoBounds{south, west, south + tile_lat_size, west + tile_lon_size};
}

auto Cdb1GlobalGrid::TilesForBounds(const GeoBounds& bounds, int lod) const
    -> std::vector<TileAddress> {
  std::vector<TileAddress> result;

  // Iterate over geocells that intersect the bounds.
  int lat_min = static_cast<int>(std::floor(bounds.south));
  int lat_max = static_cast<int>(std::floor(bounds.north - 1e-10));
  int lon_min = static_cast<int>(std::floor(bounds.west));
  int lon_max = static_cast<int>(std::floor(bounds.east - 1e-10));

  lat_min = std::clamp(lat_min, -90, 89);
  lat_max = std::clamp(lat_max, -90, 89);
  lon_min = std::clamp(lon_min, -180, 179);
  lon_max = std::clamp(lon_max, -180, 179);

  for (int lat = lat_min; lat <= lat_max; ++lat) {
    double abs_lat = std::abs(static_cast<double>(lat));
    int coal = CoalescenceForLatitude(abs_lat);

    // Snap lon_min to coalescence boundary.
    int aligned_lon_min = lon_min - ((lon_min % coal) + coal) % coal;
    // Handle negative modulo correctly: we want the largest multiple of coal
    // that is <= lon_min.
    if (lon_min >= 0) {
      aligned_lon_min = (lon_min / coal) * coal;
    } else {
      aligned_lon_min =
          ((lon_min - coal + 1) / coal) * coal;
    }

    for (int lon = aligned_lon_min; lon <= lon_max; lon += coal) {
      Geocell cell{lat, lon};

      if (lod <= 0) {
        result.push_back(TileAddress{lod, cell, 0, 0});
      } else {
        // Determine sub-tiles within this geocell that intersect bounds.
        int tiles_per_side = 1 << lod;
        double tile_lat_size = 1.0 / tiles_per_side;
        double tile_lon_size = static_cast<double>(coal) / tiles_per_side;

        int row_min = static_cast<int>(
            std::floor((bounds.south - lat) / tile_lat_size));
        int row_max = static_cast<int>(
            std::floor((bounds.north - 1e-10 - lat) / tile_lat_size));
        int col_min = static_cast<int>(
            std::floor((bounds.west - lon) / tile_lon_size));
        int col_max = static_cast<int>(
            std::floor((bounds.east - 1e-10 - lon) / tile_lon_size));

        row_min = std::clamp(row_min, 0, tiles_per_side - 1);
        row_max = std::clamp(row_max, 0, tiles_per_side - 1);
        col_min = std::clamp(col_min, 0, tiles_per_side - 1);
        col_max = std::clamp(col_max, 0, tiles_per_side - 1);

        for (int r = row_min; r <= row_max; ++r) {
          for (int c = col_min; c <= col_max; ++c) {
            result.push_back(TileAddress{lod, cell, r, c});
          }
        }
      }
    }
  }

  return result;
}

}  // namespace cdbapi::tiling
