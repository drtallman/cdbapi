#pragma once

#include <compare>
#include <string>

#include "cdbapi/tiling/geocell.h"

namespace cdbapi::tiling {

// Uniquely identifies a tile within a CDB datastore.
// At LOD <= 0: tile_row and tile_col are always 0 (one tile per geocell).
// At LOD >= 1: tile_row and tile_col identify the sub-tile within the geocell
// (quad-tree subdivision: 2^lod x 2^lod tiles per geocell).
struct TileAddress {
  int lod;            // Level of detail: -10 to 23
  Geocell geocell;    // Which 1-degree cell
  int tile_row = 0;   // Sub-tile row (0 at LOD <= 0)
  int tile_col = 0;   // Sub-tile col (0 at LOD <= 0)

  auto operator<=>(const TileAddress&) const = default;
};

// Format a TileAddress for use in file paths.
auto FormatTileAddress(const TileAddress& addr) -> std::string;

// Format an LOD value for use in directory names (e.g., "lod_00", "lod_neg03").
auto FormatLod(int lod) -> std::string;

}  // namespace cdbapi::tiling
