#pragma once

namespace cdbapi::tiling {

// Returns the CDB1GlobalGrid coalescence coefficient for a given absolute
// latitude. The coalescence coefficient determines how many 1-degree longitude
// cells are merged into a single physical tile at high latitudes.
//
// Zone table:
//   |lat| in [0, 60)   -> 1  (no coalescence)
//   |lat| in [60, 70)  -> 2
//   |lat| in [70, 75)  -> 3
//   |lat| in [75, 80)  -> 4
//   |lat| in [80, 89)  -> 6
//   |lat| in [89, 90]  -> 12
constexpr auto CoalescenceForLatitude(double abs_lat) -> int {
  if (abs_lat < 60.0) return 1;
  if (abs_lat < 70.0) return 2;
  if (abs_lat < 75.0) return 3;
  if (abs_lat < 80.0) return 4;
  if (abs_lat < 89.0) return 6;
  return 12;
}

// Returns the zone number (1-6) for a given absolute latitude.
constexpr auto ZoneForLatitude(double abs_lat) -> int {
  if (abs_lat < 60.0) return 1;
  if (abs_lat < 70.0) return 2;
  if (abs_lat < 75.0) return 3;
  if (abs_lat < 80.0) return 4;
  if (abs_lat < 89.0) return 5;
  return 6;
}

// Returns the longitude width (in degrees) of a geocell at a given absolute
// latitude. Equal to the coalescence coefficient.
constexpr auto GeocellLonWidth(double abs_lat) -> int {
  return CoalescenceForLatitude(abs_lat);
}

}  // namespace cdbapi::tiling
