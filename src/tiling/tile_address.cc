#include "cdbapi/tiling/tile_address.h"

#include <cmath>
#include <format>

#include "cdbapi/tiling/geocell.h"

namespace cdbapi::tiling {

auto FormatLod(int lod) -> std::string {
  if (lod < 0) {
    return std::format("lod_neg{:02d}", -lod);
  }
  return std::format("lod_{:02d}", lod);
}

auto FormatTileAddress(const TileAddress& addr) -> std::string {
  std::string geocell_str = FormatGeocell(addr.geocell);
  if (addr.lod <= 0) {
    return geocell_str;
  }
  return std::format("{}_r{}_c{}", geocell_str, addr.tile_row, addr.tile_col);
}

}  // namespace cdbapi::tiling
