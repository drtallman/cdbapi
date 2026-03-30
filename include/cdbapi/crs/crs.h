#pragma once

#include <optional>
#include <string>

namespace cdbapi::crs {

// CRS definition for a CDB datastore.
// Stores the EPSG code and can produce WKT-2 representations.
class Crs {
 public:
  explicit Crs(int epsg_code, std::optional<int> vertical_epsg = std::nullopt);

  auto EpsgCode() const -> int { return epsg_code_; }
  auto VerticalEpsgCode() const -> std::optional<int> {
    return vertical_epsg_code_;
  }

  // Returns the WKT-2 string for this CRS (via PROJ).
  auto ToWkt2() const -> std::string;

  // Returns true if this is a 3D CRS (has vertical component).
  auto Is3d() const -> bool { return vertical_epsg_code_.has_value(); }

 private:
  int epsg_code_;
  std::optional<int> vertical_epsg_code_;
};

}  // namespace cdbapi::crs
