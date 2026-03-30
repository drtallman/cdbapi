#include "cdbapi/tiling/geocell.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace cdbapi::tiling {

auto GeocellFromCoordinate(double latitude, double longitude) -> Geocell {
  int lat = static_cast<int>(std::floor(latitude));
  int lon = static_cast<int>(std::floor(longitude));

  // Clamp to valid ranges.
  lat = std::clamp(lat, -90, 89);
  lon = std::clamp(lon, -180, 179);

  return Geocell{lat, lon};
}

auto FormatGeocell(const Geocell& cell) -> std::string {
  char lat_prefix = cell.lat >= 0 ? 'n' : 's';
  char lon_prefix = cell.lon >= 0 ? 'e' : 'w';
  int abs_lat = std::abs(cell.lat);
  int abs_lon = std::abs(cell.lon);

  return std::format("{}{:02d}_{}{:03d}",
                     lat_prefix, abs_lat, lon_prefix, abs_lon);
}

auto IsValid(const Geocell& cell) -> bool {
  return cell.lat >= -90 && cell.lat <= 89 &&
         cell.lon >= -180 && cell.lon <= 179;
}

}  // namespace cdbapi::tiling
