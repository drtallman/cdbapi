#pragma once

#include <compare>
#include <string>

namespace cdbapi::tiling {

// A geocell is a 1-degree x 1-degree cell on the globe, identified by the
// integer floor of its southwest corner latitude and longitude.
// Latitude range: [-90, 89], Longitude range: [-180, 179].
struct Geocell {
  int lat;  // Floor of latitude (-90 to 89)
  int lon;  // Floor of longitude (-180 to 179)

  auto operator<=>(const Geocell&) const = default;
};

// Creates a Geocell from a geographic coordinate (decimal degrees).
// Returns the geocell containing the given point.
auto GeocellFromCoordinate(double latitude, double longitude) -> Geocell;

// Returns the geocell string in format like "n32_w118" or "s01_e005".
auto FormatGeocell(const Geocell& cell) -> std::string;

// Returns true if the geocell has valid lat/lon ranges.
auto IsValid(const Geocell& cell) -> bool;

}  // namespace cdbapi::tiling
