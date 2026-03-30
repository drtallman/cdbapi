#include "cdbapi/crs/crs.h"

#include <cstdlib>
#include <filesystem>
#include <format>

#include "ogr_spatialref.h"

namespace cdbapi::crs {

namespace {

// Hardcoded WKT2 for EPSG:4326 — used as fallback when PROJ db is
// unavailable at runtime.
constexpr const char* kWkt2Epsg4326 =
    "GEOGCRS[\"WGS 84\","
    "DATUM[\"World Geodetic System 1984\","
    "ELLIPSOID[\"WGS 84\",6378137,298.257223563,"
    "LENGTHUNIT[\"metre\",1]]],"
    "PRIMEM[\"Greenwich\",0,"
    "ANGLEUNIT[\"degree\",0.0174532925199433]],"
    "CS[ellipsoidal,2],"
    "AXIS[\"geodetic latitude (Lat)\",north,ORDER[1],"
    "ANGLEUNIT[\"degree\",0.0174532925199433]],"
    "AXIS[\"geodetic longitude (Lon)\",east,ORDER[2],"
    "ANGLEUNIT[\"degree\",0.0174532925199433]],"
    "ID[\"EPSG\",4326]]";

// Try to locate proj.db from the executable's vcpkg install and set
// PROJ_DATA so that PROJ's database lookup succeeds.
void EnsureProjData() {
  static bool done = [] {
    // Already set by user environment?
    if (std::getenv("PROJ_DATA")) return true;

    // Check common vcpkg-relative paths from the executable location.
    // vcpkg_installed/x64-windows/share/proj is the typical location.
    auto exe = std::filesystem::current_path();
    for (auto dir = exe; dir.has_parent_path() && dir != dir.parent_path();
         dir = dir.parent_path()) {
      auto candidate = dir / "vcpkg_installed" / "x64-windows" / "share" / "proj";
      if (std::filesystem::exists(candidate / "proj.db")) {
        // Use _putenv_s on Windows.
#ifdef _WIN32
        _putenv_s("PROJ_DATA", candidate.string().c_str());
#else
        setenv("PROJ_DATA", candidate.string().c_str(), 0);
#endif
        return true;
      }
    }
    return false;
  }();
  (void)done;
}

}  // namespace

Crs::Crs(int epsg_code, std::optional<int> vertical_epsg)
    : epsg_code_(epsg_code), vertical_epsg_code_(vertical_epsg) {}

auto Crs::ToWkt2() const -> std::string {
  EnsureProjData();

  OGRSpatialReference srs;

  if (vertical_epsg_code_.has_value()) {
    OGRSpatialReference horizontal;
    horizontal.importFromEPSG(epsg_code_);

    OGRSpatialReference vertical;
    vertical.importFromEPSG(vertical_epsg_code_.value());

    srs.SetCompoundCS(
        std::format("EPSG:{}+{}", epsg_code_, vertical_epsg_code_.value())
            .c_str(),
        &horizontal, &vertical);
  } else {
    srs.importFromEPSG(epsg_code_);
  }

  char* wkt = nullptr;
  const char* options[] = {"FORMAT=WKT2_2019", nullptr};
  srs.exportToWkt(&wkt, options);

  std::string result;
  if (wkt) {
    result = wkt;
    CPLFree(wkt);
  }

  // Fallback: if PROJ couldn't produce WKT (e.g. proj.db not found),
  // use hardcoded WKT for well-known CRS codes.
  if (result.empty() && epsg_code_ == 4326 &&
      !vertical_epsg_code_.has_value()) {
    result = kWkt2Epsg4326;
  }

  return result;
}

}  // namespace cdbapi::crs
